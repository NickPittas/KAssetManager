//! ComfyUI generation parameters from the `prompt` (API graph) and `workflow` (editor graph) JSON
//! that ComfyUI stores in PNG text chunks. Every id/link comes from the file: lookups are
//! bounded (depth cap, visited set) so a cyclic or hostile graph cannot loop.

use std::collections::HashSet;

use serde_json::{Map, Value};

use crate::metadata::ComfyMetadata;

const MAX_DEPTH: usize = 24;

/// Parse `prompt` / `workflow` JSON text. `Some` when at least one of them is present (the raw JSON
/// is kept either way); the generation fields are filled from `prompt`, else from `workflow`.
pub(crate) fn parse(prompt: Option<&str>, workflow: Option<&str>) -> Option<ComfyMetadata> {
    if prompt.is_none() && workflow.is_none() {
        return None;
    }
    let mut meta = ComfyMetadata {
        prompt_json: prompt.map(str::to_owned),
        workflow_json: workflow.map(str::to_owned),
        ..Default::default()
    };
    let from_prompt = prompt
        .and_then(|p| serde_json::from_str::<Value>(p).ok())
        .is_some_and(|v| fill_from_prompt(&v, &mut meta));
    if !from_prompt && let Some(v) = workflow.and_then(|w| serde_json::from_str::<Value>(w).ok()) {
        fill_from_workflow(&v, &mut meta);
    }
    Some(meta)
}

// ---------------------------------------------------------------- API ("prompt") graph

type Nodes = Map<String, Value>;

fn link_node<'a>(nodes: &'a Nodes, v: &Value) -> Option<&'a Value> {
    let a = v.as_array()?;
    let id = match a.first()? {
        Value::String(s) => s.clone(),
        Value::Number(n) => n.to_string(),
        _ => return None,
    };
    nodes.get(&id)
}

fn is_link(v: &Value) -> bool {
    v.as_array().is_some_and(|a| a.len() == 2)
}

fn class(node: &Value) -> &str {
    node.get("class_type").and_then(Value::as_str).unwrap_or("")
}

fn input<'a>(node: &'a Value, name: &str) -> Option<&'a Value> {
    node.get("inputs")?.get(name)
}

/// The literal behind input `name`: the value itself, or (for a link) the `value`-style widget of the
/// primitive node it points at.
fn literal<'a>(nodes: &'a Nodes, node: &'a Value, name: &str) -> Option<&'a Value> {
    let mut v = input(node, name)?;
    for _ in 0..4 {
        if !is_link(v) {
            return Some(v);
        }
        let n = link_node(nodes, v)?;
        v = ["value", "int", "float", "string", "seed", "text"]
            .iter()
            .find_map(|k| input(n, k))?;
    }
    None
}

fn as_i64(v: &Value) -> Option<i64> {
    v.as_i64().or_else(|| {
        v.as_f64()
            .filter(|f| f.is_finite() && f.fract() == 0.0 && f.abs() < 9.0e18)
            .map(|f| f as i64)
    })
}

fn sorted(nodes: &Nodes) -> Vec<(&String, &Value)> {
    let mut v: Vec<_> = nodes.iter().collect();
    v.sort_by(|a, b| {
        (a.0.parse::<u64>().unwrap_or(u64::MAX), a.0)
            .cmp(&(b.0.parse::<u64>().unwrap_or(u64::MAX), b.0))
    });
    v
}

fn fill_from_prompt(root: &Value, out: &mut ComfyMetadata) -> bool {
    let Some(nodes) = root.as_object() else {
        return false;
    };
    let ordered = sorted(nodes);
    let sampler = ordered
        .iter()
        .map(|(_, n)| *n)
        .find(|n| class(n).starts_with("KSampler"));
    let custom = ordered
        .iter()
        .map(|(_, n)| *n)
        .find(|n| matches!(class(n), "SamplerCustom" | "SamplerCustomAdvanced"));
    let mut found = false;
    if let Some(s) = sampler {
        found = true;
        out.seed = ["seed", "noise_seed"]
            .iter()
            .find_map(|k| literal(nodes, s, k))
            .and_then(as_i64);
        out.steps = literal(nodes, s, "steps").and_then(as_i64);
        out.cfg = literal(nodes, s, "cfg").and_then(Value::as_f64);
        out.sampler = literal(nodes, s, "sampler_name")
            .and_then(Value::as_str)
            .map(str::to_owned);
        out.scheduler = literal(nodes, s, "scheduler")
            .and_then(Value::as_str)
            .map(str::to_owned);
        out.positive = input(s, "positive").and_then(|v| text_of(nodes, v, 0, &mut HashSet::new()));
        out.negative = input(s, "negative").and_then(|v| text_of(nodes, v, 0, &mut HashSet::new()));
        out.model = input(s, "model")
            .and_then(|v| model_of(nodes, v))
            .or_else(|| any_checkpoint(&ordered));
    } else if let Some(s) = custom {
        found = true;
        // SamplerCustom carries seed/cfg itself; SamplerCustomAdvanced spreads them over helper nodes.
        out.seed = ["noise_seed", "seed"]
            .iter()
            .find_map(|k| literal(nodes, s, k))
            .and_then(as_i64);
        out.cfg = literal(nodes, s, "cfg").and_then(Value::as_f64);
        out.positive = input(s, "positive").and_then(|v| text_of(nodes, v, 0, &mut HashSet::new()));
        out.negative = input(s, "negative").and_then(|v| text_of(nodes, v, 0, &mut HashSet::new()));
        let via = |key: &str| input(s, key).and_then(|v| link_node(nodes, v));
        if let Some(n) = via("noise") {
            out.seed = out.seed.or_else(|| {
                ["noise_seed", "seed"]
                    .iter()
                    .find_map(|k| literal(nodes, n, k))
                    .and_then(as_i64)
            });
        }
        if let Some(g) = via("guider") {
            out.cfg = out
                .cfg
                .or_else(|| literal(nodes, g, "cfg").and_then(Value::as_f64));
            out.positive = out.positive.take().or_else(|| {
                input(g, "positive")
                    .or_else(|| input(g, "conditioning"))
                    .and_then(|v| text_of(nodes, v, 0, &mut HashSet::new()))
            });
            out.negative = out.negative.take().or_else(|| {
                input(g, "negative").and_then(|v| text_of(nodes, v, 0, &mut HashSet::new()))
            });
            out.model = input(g, "model").and_then(|v| model_of(nodes, v));
        }
        if let Some(k) = via("sampler") {
            out.sampler = literal(nodes, k, "sampler_name")
                .and_then(Value::as_str)
                .map(str::to_owned);
        }
        if let Some(sig) = via("sigmas") {
            out.steps = literal(nodes, sig, "steps").and_then(as_i64);
            out.scheduler = literal(nodes, sig, "scheduler")
                .and_then(Value::as_str)
                .map(str::to_owned);
        }
        out.model = out.model.take().or_else(|| any_checkpoint(&ordered));
    }
    if !found {
        // No sampler: still show the first text prompt and checkpoint the graph names.
        out.positive = ordered
            .iter()
            .map(|(_, n)| *n)
            .filter(|n| class(n).contains("CLIPTextEncode"))
            .find_map(|n| clip_text(nodes, n));
        out.model = any_checkpoint(&ordered);
        found = out.positive.is_some() || out.model.is_some();
    }
    found
}

fn clip_text(nodes: &Nodes, node: &Value) -> Option<String> {
    let get = |k: &str| {
        literal(nodes, node, k)
            .and_then(Value::as_str)
            .map(str::to_owned)
    };
    match (get("text"), get("text_g"), get("text_l")) {
        (Some(t), _, _) => Some(t),
        (None, Some(g), Some(l)) if g != l => Some(format!("{g}\n{l}")),
        (None, Some(g), _) => Some(g),
        (None, None, l) => l,
    }
}

/// The prompt text feeding conditioning input `v`, following pass-through nodes.
fn text_of(nodes: &Nodes, v: &Value, depth: usize, seen: &mut HashSet<String>) -> Option<String> {
    if depth > MAX_DEPTH {
        return None;
    }
    let node = link_node(nodes, v)?;
    let id = v.as_array()?.first()?.to_string();
    if !seen.insert(id) {
        return None;
    }
    if (class(node).contains("CLIPTextEncode")
        || input(node, "text").is_some_and(|t| !is_link(t) && t.is_string()))
        && let Some(t) = clip_text(nodes, node)
    {
        return Some(t);
    }
    [
        "conditioning",
        "conditioning_1",
        "conditioning_to",
        "positive",
        "negative",
        "cond",
    ]
    .iter()
    .filter_map(|k| input(node, k))
    .find_map(|n| text_of(nodes, n, depth + 1, seen))
}

fn model_name(node: &Value) -> Option<String> {
    ["ckpt_name", "unet_name", "model_name", "diffusion_model"]
        .iter()
        .find_map(|k| input(node, k)?.as_str())
        .map(str::to_owned)
}

/// The checkpoint feeding model input `v` (through LoRA/patch nodes).
fn model_of(nodes: &Nodes, v: &Value) -> Option<String> {
    let mut cur = v;
    let mut seen = HashSet::new();
    for _ in 0..MAX_DEPTH {
        let node = link_node(nodes, cur)?;
        if !seen.insert(cur.as_array()?.first()?.to_string()) {
            return None;
        }
        if let Some(n) = model_name(node) {
            return Some(n);
        }
        cur = input(node, "model")?;
    }
    None
}

fn any_checkpoint(ordered: &[(&String, &Value)]) -> Option<String> {
    ordered
        .iter()
        .map(|(_, n)| *n)
        .filter(|n| {
            let c = class(n);
            c.contains("CheckpointLoader") || c.contains("UNETLoader") || c.contains("UnetLoader")
        })
        .find_map(model_name)
}

// ---------------------------------------------------------------- editor ("workflow") graph

struct Graph<'a> {
    nodes: Vec<&'a Value>,
    /// (link id, origin node id)
    links: Vec<(i64, i64)>,
}

impl<'a> Graph<'a> {
    fn node(&self, id: i64) -> Option<&'a Value> {
        self.nodes
            .iter()
            .copied()
            .find(|n| n.get("id").and_then(Value::as_i64) == Some(id))
    }
    /// Origin node of the link wired into `node`'s input `name`.
    fn upstream(&self, node: &Value, name: &str) -> Option<&'a Value> {
        let link = node
            .get("inputs")?
            .as_array()?
            .iter()
            .find(|i| i.get("name").and_then(Value::as_str) == Some(name))?
            .get("link")?
            .as_i64()?;
        let origin = self.links.iter().find(|(id, _)| *id == link)?.1;
        self.node(origin)
    }
    fn text(&self, node: &Value, depth: usize) -> Option<String> {
        if depth > MAX_DEPTH {
            return None;
        }
        let ty = node.get("type").and_then(Value::as_str).unwrap_or("");
        if ty.contains("CLIPTextEncode") {
            let wv = node.get("widgets_values")?.as_array()?;
            return wv.first()?.as_str().map(str::to_owned);
        }
        [
            "conditioning",
            "conditioning_1",
            "conditioning_to",
            "positive",
            "negative",
        ]
        .iter()
        .filter_map(|k| self.upstream(node, k))
        .find_map(|n| self.text(n, depth + 1))
    }
    fn model(&self, node: &Value) -> Option<String> {
        let mut cur = node;
        for _ in 0..MAX_DEPTH {
            let ty = cur.get("type").and_then(Value::as_str).unwrap_or("");
            if ty.contains("CheckpointLoader")
                || ty.contains("UNETLoader")
                || ty.contains("UnetLoader")
            {
                return cur
                    .get("widgets_values")?
                    .as_array()?
                    .first()?
                    .as_str()
                    .map(str::to_owned);
            }
            cur = self.upstream(cur, "model")?;
        }
        None
    }
}

fn fill_from_workflow(root: &Value, out: &mut ComfyMetadata) {
    let Some(nodes) = root.get("nodes").and_then(Value::as_array) else {
        return;
    };
    let links = root
        .get("links")
        .and_then(Value::as_array)
        .map(|l| {
            l.iter()
                .filter_map(|e| e.as_array())
                .filter_map(|e| Some((e.first()?.as_i64()?, e.get(1)?.as_i64()?)))
                .collect()
        })
        .unwrap_or_default();
    let g = Graph {
        nodes: nodes.iter().collect(),
        links,
    };
    let ty = |n: &Value| {
        n.get("type")
            .and_then(Value::as_str)
            .unwrap_or("")
            .to_owned()
    };
    let Some(s) = g
        .nodes
        .iter()
        .copied()
        .find(|n| ty(n).starts_with("KSampler"))
    else {
        out.positive = g
            .nodes
            .iter()
            .copied()
            .filter(|n| ty(n).contains("CLIPTextEncode"))
            .find_map(|n| g.text(n, 0));
        out.model = g.nodes.iter().copied().find_map(|n| g.model(n));
        return;
    };
    let wv: Vec<&Value> = s
        .get("widgets_values")
        .and_then(Value::as_array)
        .map(|a| a.iter().collect())
        .unwrap_or_default();
    let w = |i: usize| wv.get(i).copied();
    // KSampler: seed, control, steps, cfg, sampler, scheduler, denoise
    // KSamplerAdvanced: add_noise, noise_seed, control, steps, cfg, sampler, scheduler, start, end, return
    let base = if ty(s) == "KSamplerAdvanced" { 1 } else { 0 };
    out.seed = w(base).and_then(as_i64);
    out.steps = w(base + 2).and_then(as_i64);
    out.cfg = w(base + 3).and_then(Value::as_f64);
    out.sampler = w(base + 4).and_then(Value::as_str).map(str::to_owned);
    out.scheduler = w(base + 5).and_then(Value::as_str).map(str::to_owned);
    out.positive = g.upstream(s, "positive").and_then(|n| g.text(n, 0));
    out.negative = g.upstream(s, "negative").and_then(|n| g.text(n, 0));
    out.model = g
        .upstream(s, "model")
        .and_then(|n| g.model(n))
        .or_else(|| g.nodes.iter().copied().find_map(|n| g.model(n)));
}

#[cfg(test)]
mod tests {
    // Vendored from GalleryBrowser 55a4483: test assertions may panic by design. See crates/VENDORED.md.
    #![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
    use super::*;

    const PROMPT: &str = r#"{
      "3": {"class_type":"KSampler","inputs":{"seed":156680208700286,"steps":20,"cfg":8.0,"sampler_name":"euler","scheduler":"normal","denoise":1,
            "model":["10",0],"positive":["6",0],"negative":["7",0],"latent_image":["5",0]}},
      "4": {"class_type":"CheckpointLoaderSimple","inputs":{"ckpt_name":"v1-5-pruned.safetensors"}},
      "10": {"class_type":"LoraLoader","inputs":{"model":["4",0],"clip":["4",1],"lora_name":"x.safetensors"}},
      "6": {"class_type":"CLIPTextEncode","inputs":{"text":"a beautiful beach","clip":["4",1]}},
      "7": {"class_type":"CLIPTextEncode","inputs":{"text":"blurry, ugly","clip":["4",1]}}
    }"#;

    #[test]
    fn parses_ksampler_graph_through_lora() {
        let m = parse(Some(PROMPT), None).expect("comfy");
        assert_eq!(m.seed, Some(156680208700286));
        assert_eq!(m.steps, Some(20));
        assert_eq!(m.cfg, Some(8.0));
        assert_eq!(m.sampler.as_deref(), Some("euler"));
        assert_eq!(m.scheduler.as_deref(), Some("normal"));
        assert_eq!(m.positive.as_deref(), Some("a beautiful beach"));
        assert_eq!(m.negative.as_deref(), Some("blurry, ugly"));
        assert_eq!(m.model.as_deref(), Some("v1-5-pruned.safetensors"));
        assert_eq!(m.prompt_json.as_deref(), Some(PROMPT));
    }

    #[test]
    fn follows_primitive_seed_and_conditioning_passthrough() {
        let p = r#"{
          "1": {"class_type":"PrimitiveInt","inputs":{"value":42}},
          "2": {"class_type":"KSamplerAdvanced","inputs":{"noise_seed":["1",0],"steps":9,"cfg":3.5,"sampler_name":"dpmpp_2m","scheduler":"karras",
                "positive":["3",0],"negative":["5",0],"model":["6",0]}},
          "3": {"class_type":"FluxGuidance","inputs":{"conditioning":["4",0],"guidance":3.5}},
          "4": {"class_type":"CLIPTextEncode","inputs":{"text":"hello"}},
          "5": {"class_type":"CLIPTextEncode","inputs":{"text":""}},
          "6": {"class_type":"UNETLoader","inputs":{"unet_name":"flux.safetensors"}}
        }"#;
        let m = parse(Some(p), None).expect("comfy");
        assert_eq!((m.seed, m.steps), (Some(42), Some(9)));
        assert_eq!(m.positive.as_deref(), Some("hello"));
        assert_eq!(m.negative.as_deref(), Some(""));
        assert_eq!(m.model.as_deref(), Some("flux.safetensors"));
    }

    #[test]
    fn workflow_graph_is_the_fallback() {
        let wf = r#"{"nodes":[
          {"id":3,"type":"KSampler","widgets_values":[7,"randomize",25,6.5,"euler_ancestral","simple",1.0],
           "inputs":[{"name":"model","link":1},{"name":"positive","link":2},{"name":"negative","link":3}]},
          {"id":4,"type":"CheckpointLoaderSimple","widgets_values":["sdxl.safetensors"]},
          {"id":6,"type":"CLIPTextEncode","widgets_values":["pos text"]},
          {"id":7,"type":"CLIPTextEncode","widgets_values":["neg text"]}],
          "links":[[1,4,0,3,0,"MODEL"],[2,6,0,3,1,"CONDITIONING"],[3,7,0,3,2,"CONDITIONING"]]}"#;
        let m = parse(Some("not json"), Some(wf)).expect("comfy");
        assert_eq!((m.seed, m.steps, m.cfg), (Some(7), Some(25), Some(6.5)));
        assert_eq!(m.sampler.as_deref(), Some("euler_ancestral"));
        assert_eq!(m.scheduler.as_deref(), Some("simple"));
        assert_eq!(
            (m.positive.as_deref(), m.negative.as_deref()),
            (Some("pos text"), Some("neg text"))
        );
        assert_eq!(m.model.as_deref(), Some("sdxl.safetensors"));
        assert_eq!(m.prompt_json.as_deref(), Some("not json"));
    }

    #[test]
    fn cyclic_and_garbage_graphs_terminate() {
        let p = r#"{"1":{"class_type":"KSampler","inputs":{"positive":["2",0],"model":["2",0]}},
                    "2":{"class_type":"Foo","inputs":{"conditioning":["1",0],"model":["1",0]}}}"#;
        let m = parse(Some(p), None).expect("comfy");
        assert_eq!((m.positive, m.model), (None, None));
        assert!(parse(Some("[1,2,3]"), Some("{")).is_some());
        assert!(parse(None, None).is_none());
    }
}
