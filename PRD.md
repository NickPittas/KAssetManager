Product Overview

Desktop asset manager for local/NAS video & photo libraries providing LLM-assisted descriptions, user-defined tagging, drag/drop workflows, and non-destructive organization without redundant media derivatives.
Personas: Solo Creator (fast curation, creative app workflows) and Studio Librarian (coordinating 6–12 concurrent collaborators).
Goals: 90% of ingestable assets auto-tagged within 2 minutes when LLM reachable; <2s search over 50k assets; <1% metadata loss across 6 months; 80% thumbnail requests served via real-time decode; ship only production-ready code (strict “no mochap/mock” policy).
Key Use Cases

Ingestion from local drives or NAS with checksum dedupe, sidecar metadata import, Untagged smart view for assets lacking descriptions/tags, and continuous watch folders.
LLM-driven or manual tagging: Ollama (Qwen 3 VL) / LM Studio integration with keyword rules mapping to user-defined hierarchical tags/categories; manual editing pulls assets out of Untagged automatically.
Browsing: scalable thumbnail grid/list, filters by tag/category/folder/codec/duration/date/AI confidence/Untagged status, rating, favorites, and original folder context.
Drag & drop: assign metadata, move/copy with prompts, reorder collections, and drop to external apps (Nuke/Premiere/Resolve) like Windows Explorer.
Metadata export/import through CSV/JSON/XML; single shared library per user per project—no multi-library management.
Feature Scope

Ingestion & Watch: batch import, background scanning tolerant of NAS latency, failed AI requests routed to Untagged with retry queue.
Metadata Model: user-defined tags and categories (hierarchical), versioned descriptions (AI/manual with confidence scoring), audit trail, CSV taxonomy import/export.
Organization: manual & smart collections, ratings/favorites, persistent Untagged smart collection, original folder preservation, non-destructive virtual structure with optional physical move (prompted).
Preview Pipeline: FFMPEG real-time decode leveraging hardware acceleration (NVDEC/AMF/Metal/VAAPI); CPU fallback; proxy creation only when decode unsupported.
Thumbnail Cache: on-demand generation with in-memory cache plus persistent disk cache stored by default at <user-home>\KAssets\cache; users can customize location and quota; cache remains local (not shared).
Search/Filter: indexed full-text over descriptions, Boolean tag/category filters, folder origin, codec/resolution/duration/date, Untagged filter, ratings, confidence thresholds.
Collaboration: optimistic locking, live change notifications, audit logging for 6–12 concurrent users; no authentication layer or SLA requirements.
Integrations: CSV/JSON/XML export/import; no plugin API or command-line ingestion scripting.
Roadmap handling: missing features surfaced solely as textual roadmap/backlog entries—no placeholder backend/UI code.
Non-Functional Requirements

Hardware baseline: ≥16GB RAM; NVIDIA or AMD GPU with hardware decode guarantees 500ms thumbnail generation target.
Performance: handle up to 1TB/day ingestion, <2s search, virtualized UI lists for large libraries, GPU-aware decode scheduling.
Reliability: resumable imports, transactional metadata writes, NAS reconnection logic, offline AI retry queue, Untagged state persisted.
Security: rely on OS user accounts; encrypted credential store for NAS paths; permission validation; comprehensive audit logging.
Accessibility & Intl: keyboard parity, screen reader support, high-contrast & color-blind-safe themes, locale-aware text, UTF-8 metadata.
Telemetry (opt-in): ingest throughput, AI success rate, hardware decode usage with local diagnostics viewer.
Architecture

Desktop: Electron + React (TypeScript), Redux Toolkit, Tailwind, i18next; context menus for drag/drop verbs.
Backend: Node.js (NestJS) services exposing REST/GraphQL APIs; BullMQ workers for ingestion, decoding, AI jobs; WebSocket/IPC push updates.
Media Processing: bundled FFMPEG/FFprobe using GPU decode paths with CPU fallback; policy-controlled proxy builder for unsupported codecs.
Data Stores: PostgreSQL (JSONB metadata, trigram FTS, pg_vector roadmap), Redis for queues/cache, local disk cache per user for thumbnails.
File Management: chokidar watchers across local/NAS paths; path abstraction layer references canonical asset locations to avoid unintended moves.
Data Model Highlights

asset, tag, category, asset_tag, asset_category, collection, collection_item, description_version, thumbnail_cache, import_job, llm_request, llm_failure, keyword_rule, audit_log, derived is_untagged view.
Thumbnail cache records include cache_path, persistent_flag, quota_bucket, supporting user-defined location/quota.
Audit log captures metadata edits, tagging actions, physical moves with user context.
LLM Workflow

Frame extraction in memory, caching frames only when needed; prompt enriched with folder context and taxonomy hints; keyword rules normalize outputs; user approves AI suggestions before commit.
Failures mark asset Untagged, log reason, schedule retries; manual tagging overrides AI queue; version history records AI vs manual descriptions.
User Experience

Layout: sidebar (Sources, Untagged, Tags, Categories, Collections), central grid/list with resizable thumbnails, right metadata drawer.
Untagged management: prominent badge, batch selection, drag-to-tag/category removal.
Editing: inline tag chips, category tree search, description editor with version dropdown, keyboard shortcuts (Ctrl+T, Ctrl+F, Space for preview).
Move/Copy dialogs match OS conventions; OS-level drag-out provides file handles to external creative apps.
Multi-user awareness: presence indicators, conflict dialogs, change history viewer.
Operations & Deployment

Installer bundles FFMPEG and services, validates GPU decode; installs background worker (Windows Service/macOS LaunchDaemon).
Updates: delta updates with rollback, stable/beta channels, offline installers.
Backup/Restore: PostgreSQL dump, Redis snapshot, thumbnail cache manifest (location-aware); optional encryption; guided restore flow.
Diagnostics bundle exports logs/configs/backups with PII scrubbing.
Telemetry remains optional and user-controlled.
Risks & Mitigations

Unsupported codecs → detect at ingest, prompt for targeted proxy creation or manual override.
NAS latency → async loading, configurable prefetch, persistent local caching.
AI backlog → manual tagging workflows, retry prioritization, Untagged filters.
Taxonomy drift → CSV import/export, merge/rename tools, audited changes.
Concurrent edits → optimistic locking and detailed conflict resolution UI.
Backlog Items (Text Only, No Code Stubs)

Evaluate future authentication toggle requirements if stakeholder priorities change.
Open Questions

None; all prior queries resolved.
Next Steps

Produce wireframes for ingestion flow, Untagged management, drag/drop prompts, and metadata drawer.
Execute technical spikes: NAS watcher throughput, GPU decode benchmarking for target codec set, AI retry pipeline with configurable cache.
Define MVP backlog (ingestion, Untagged workflow, manual/AI tagging, CSV I/O, real-time previews, drag/drop) and schedule incremental delivery milestones.