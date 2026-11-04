# Release versions

Release versions have three numeric components. After 0.9.99, continue with
0.9.100, 0.9.101 and so on until 1.0.0 is ready. The existing launcher compares
these components as integers, so 0.9.100 is newer than 0.9.99 and older than 1.0.0.

Avoid a fourth component or a suffix for public hotfixes. Installed launchers only
compare the first three numbers and may never offer such an update. Keep VERSION,
release tags, archive names and release notes on the same three-part version.
