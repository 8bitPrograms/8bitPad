# Pinned dependency sources

These are unchanged copies of the sources used to build this release. Their
original notices and licenses remain in each directory. Git metadata is omitted.

| Directory | Upstream revision |
| --- | --- |
| `ads` | Qt Advanced Docking System v5.1.1 / `023ce95934fecfd4cf5c672c9c404fabe0f54923` |
| `singleapplication` | SingleApplication v3.3.4 / `a3ed916f591c300e97b873fde36863fa37b49fa9` |
| `editorconfig-core-qt` | `ab62f0554abf2bbe4d45427b36a8b2f81ca7b4ab` |

The normal build fetches these revisions through CPM. To configure without
fetching them, add the following CMake arguments using absolute source paths:

```
-DCPM_ads_SOURCE=/source/8bitpad/dependency-sources/ads
-DCPM_SingleApplication_SOURCE=/source/8bitpad/dependency-sources/singleapplication
-DCPM_editorconfig_core_qt_SOURCE=/source/8bitpad/dependency-sources/editorconfig-core-qt
-DFETCHCONTENT_FULLY_DISCONNECTED=ON
```

Scintilla, Lexilla, Lua and uchardet sources are already included in `thirdparty/`.
Qt is a separately installed dynamic SDK/runtime; versioned source locations,
notices and inventories are provided under `licenses/qt/`.
