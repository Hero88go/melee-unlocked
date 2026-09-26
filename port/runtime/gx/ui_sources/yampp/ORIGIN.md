# YAMPP UI import boundary

**Status: reserved; no YAMPP code or assets are imported here yet.**

Reference repository: <https://github.com/sonsegajp/YAMPP>

Reviewed source revision: `b560eb9e64bcf07dbe5d4251483d609317fc60a3`.

Keep each future YAMPP-derived menu file, image, font, or animation in this
directory. Record the original path, exact commit, and applicable license for
each imported item before adding it to the build. Do not copy game-derived
Nintendo assets or screenshots here. Route product code through one adapter in
this directory and add that adapter to `port/CMakeLists.txt` as a distinct
source entry. Removing this directory and that entry must leave the other UI
styles buildable.
