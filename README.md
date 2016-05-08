Barst
======

The Barst server project. It has a server/client architecture
for controlling hardware commonly used in the CPL lab.

See http://matham.github.io/barst/index.html for the complete
server documentation.

Note that the project does not link to any external code directly
but instead loads driver dlls dynamically at runtime. Therefore,
these dlls needs to be present and installed on the system.

Usage
=====

Users typically just instantiate a server instance using e.g.

    start "" "barst.exe" "\\.\pipe\CPL_test" 1024 1024

in a batch file. Once instantiated, users only interact using
the client API. PyBarst is a python implementation of a Barst
client. See https://matham.github.io/pybarst/index.html for
full details.
