Barst
======

Barst is a server/client architecture for reading/writing to
devices, such as switches or cameras commonly used in the CPL lab.

It allows one central server to control the devices, while clients
request that data be sent or read from the devices.

See http://matham.github.io/barst/index.html for the complete
server documentation.

Note that the project does not link to any external code at compile time
but instead loads driver dlls dynamically at runtime. Therefore,
these dlls needs to be present and installed on the system before
specific devices can be supported.

Usage
=====

Users typically just instantiate a server instance from e.g. a batch file with

    start "" "barst.exe" "\\.\pipe\CPL_test" 1024 1024

in a batch file. Once instantiated, users only interact using
the client API over pipes. PyBarst is a python implementation
of a Barst client. See https://matham.github.io/pybarst/index.html for
full details.
