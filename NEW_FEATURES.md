# KAssetManager - New Features Roadmap

**Version:** 0.3.0+
**Status:** Implementation Phase - 5 Features Complete
**Last Updated:** 2025-11-03
**Target:** Post Phase 1 & 2 Completion

## ✅ Completed Features (v0.3.0)

The following intelligent features have been implemented and are available in the current build:

1. **✅ Feature 3: Everything Search Integration** - Ultra-fast disk-wide search with bulk import
2. **✅ Feature 6: Database Health Agent** - Automated health checks and maintenance
3. **✅ Feature 7: Bulk Rename Intelligence** - Pattern-based renaming with preview
4. **✅ Feature 9: Sequence Intelligence** - Gap detection and version tracking
5. **✅ Feature 10: Context Preserver** - Per-folder UI state persistence

See [INTELLIGENT_FEATURES.md](INTELLIGENT_FEATURES.md) for complete installation and usage instructions.

---


## Executive Summary

This document outlines the planned AI-driven and workflow enhancement features for KAssetManager. These features focus on **workflow automation, data integrity, and intelligent assistance** rather than content analysis. Each feature is designed to solve real user pain points in professional asset management pipelines.

### Design Philosophy
- **Workflow Multipliers:** Features that 10x user productivity through automation
- **Data Integrity First:** Never lose assets; always know the state of your database
- **Intelligence Over AI:** Use AI where it adds genuine value, not for novelty
- **Pipeline Integration:** Professional workflows require programmatic access

---


## ?? Feature Priority Matrix

| Priority | Feature | User Impact | Complexity | Dependencies |
|----------|---------|-------------|------------|--------------|
| **P0** | Asset Integrity Monitor | Critical | Medium | Phase 2 complete |
| **P0** | Watch Folder System | Critical | Medium | Asset Integrity |
| **P1** | Asset Manager Copilot | High | High | LLM integration |
| **P1** | Everything Search Integration | High | Low | Everything SDK |
| **P2** | Project Recovery Agent | High | Medium | Operation logging |
| **P2** | Smart Import Rules Agent | Medium | Medium | Pattern learning |
| **P3** | Database Health Agent | Medium | Low | None |
| **P3** | Bulk Rename Intelligence | Medium | Medium | Copilot |
| **P4** | API Bridge & Plugin SDK | Medium | High | REST framework |
| **P4** | Sequence Intelligence Agent | Medium | Medium | VFX-focused |
| **P4** | Context Preserver Agent | Low | Low | None |
| **P4** | Notification & Alert Agent | Low | Low | None |

---


## 📋 FEATURE 1: Asset Integrity Monitor & Watch Folder System ⭐

### Overview
**Problem:** Assets get deleted, moved, or renamed outside KAssetManager; database becomes stale; users lose track of file locations.

**Solution:** Two-pronged approach:
1. **Watch Folders (Proactive):** Monitor directories for changes, auto-import or alert
2. **Integrity Scanner (Reactive):** Validate database records against file system

### User Stories
- "Watch D:\Renders\ and auto-import any new .exr sequences with tag 'render'"
- "Watch \\server\dailies\ and notify me when new files appear"
- "Watch my Finals folder and alert me if any asset is deleted or moved"
- "Check if all my imported assets still exist on disk"
- "Find files that moved/renamed and update my database automatically"

### Technical Design

See native/qt6/src/watch_folder_monitor.h and asset_integrity_scanner.h for detailed class designs.

**Database Schema:**
- watch_folders table with path, filters, actions
- watch_folder_events for audit trail
- Asset integrity columns: file_checksum, is_missing, last_verified

### Implementation: 4 Phases (8 weeks)
- Phase 1: Core infrastructure
- Phase 2: Watch folder system with ReadDirectoryChangesW
- Phase 3: Integrity scanner with smart detection
- Phase 4: Polish and testing

---

## 📋 FEATURE 2: Asset Manager Copilot ⭐

### Overview
**Problem:** Complex operations require many clicks and steps.

**Solution:** Natural language interface with LLM function calling.

### User Stories
- "Move all 4K videos from last week to Archive"
- "Find duplicates and show disk usage"
- "Create folder structure and organize assets"

### Technical Design
- OpenAI GPT-4 or local Llama 3
- Function registry with 20+ tools
- Database: copilot_sessions, copilot_messages, copilot_function_calls

### Implementation: 4 Phases (8 weeks)

---

## 📋 FEATURE 3: Everything Search Integration ⭐

### Overview
**Problem:** Can only search imported assets, not entire disk.

**Solution:** Everything SDK integration for instant filesystem search.

### User Stories
- "Import all .exr files from D: drive created this month"
- "Search both database and disk for files"

### Technical Design
- Everything DLL loading and API calls
- Hybrid search: DB + filesystem
- Mark already-imported files

### Implementation: 3 Phases (6 weeks)

---

## 📋 FEATURES 4-11: Additional Features

### Feature 4: Project Recovery Agent
Operation logging with undo/redo. Database: operation_log table.

### Feature 5: Smart Import Rules
Learn from user behavior, suggest rules. Database: import_rules table.

### Feature 6: Database Health Agent
Automated health checks, maintenance, VACUUM suggestions.

### Feature 7: Bulk Rename Intelligence
Natural language rename with preview and safety.

### Feature 8: API Bridge & Plugin SDK
REST API + Python SDK for DCC tool integration.

### Feature 9: Sequence Intelligence
Frame gap detection, version tracking for VFX sequences.

### Feature 10: Context Preserver
Save/restore UI state per folder and session.

### Feature 11: Notifications
Windows toast notifications for background operations.

---

## 🗓️ Implementation Roadmap

### Q1 2025: Foundation
- ✅ Phase 1 & 2 complete
- [ ] Asset Integrity + Watch Folders (P0)
- [ ] Copilot MVP (P1)
- [ ] Everything Search (P1)

### Q2 2025: Workflow Automation
- [ ] Copilot Full (P1)
- [ ] Project Recovery (P2)
- [ ] Smart Import Rules (P2)
- [ ] Database Health (P3)

### Q3 2025: Professional Features
- [ ] Bulk Rename (P3)
- [ ] API Bridge (P4)
- [ ] Sequence Intelligence (P4)

### Q4 2025: Polish
- [ ] Context Preserver (P4)
- [ ] Notifications (P4)
- [ ] Plugin marketplace

---

## 🎯 Success Metrics

- 70%+ users enable Watch Folders
- 50%+ users use Copilot weekly
- Integrity scan: 1000+ assets/second
- Copilot response: <3 seconds

---

## 🔧 Technical Prerequisites

- LLM integration (OpenAI SDK + llama.cpp)
- Qt HTTP Server for REST API
- Everything SDK for search
- Background job system with QThreadPool

---

## 🚀 Getting Started

```bash
# Feature 1: Integrity Monitor
sqlite3 kassetmanager.db < migrations/003_integrity_tables.sql
cd native/qt6 && mkdir -p src/integrity
# Add watch_folder_monitor.{h,cpp}, asset_integrity_scanner.{h,cpp}
./scripts/build-windows.ps1 -Generator Ninja -Package
```

---

## 📝 Design Decisions

### Why Not Content-Analysis AI?
- Low ROI (users care about organization, not content understanding)
- High cost (GPU, large models)
- Privacy concerns
- Users already have DCC tools

### Why Copilot?
- Flexibility without UI bloat
- Feature discoverability
- 10-step workflows → 1 sentence
- Future-proof

### Local vs Cloud LLM
**Hybrid approach:**
- Local Llama 3 for simple queries
- Cloud GPT-4 for complex planning
- User choice in Settings

---

## 📚 Related Documents

- CODE_REVIEW_REPORT.md - Phase 1 & 2 analysis
- TASKS.md - Current task tracking
- PLAN.md - Remediation plan
- TECH.md - Technology stack
- DEVELOPER_GUIDE.md - Setup guide

---

**Status:** ✅ Ready for Implementation  
**Next Review:** After Feature 1 completion  
**Version:** 1.0.0
