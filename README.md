Barst
======

Barst is a server/client architecture for reading/writing to
devices, such as switches or cameras commonly used in the CPL lab.

It allows one central server to control the devices, while clients
request that data be sent or read from the devices.

See http://matham.github.io/barst/index.html for the complete
server documentation and https://matham.github.io/pybarst/index.html
for a Python client implementation.

Note that the project does not link to any external code at compile time
but instead loads driver dlls dynamically at runtime. Therefore,
these dlls needs to be present and installed on the system before
specific devices can be supported.

Usage
-----

Users typically instantiate a server instance from a batch file with e.g.

    start "" "barst.exe" "\\.\pipe\CPL_test" 1024 1024

in a batch file. Once instantiated, users interact with the server using
the client API over the provided pipe.


Architecture
------------

Each device supported by the server requires that a ``CManager`` and ``CDevice``
interface to be implemented. This defines the operations supported by the device.

A client first requests the manager for a particular device type, e.g. the ``MCDAQ``
(Measurement Computing USB DAQ) using the main server pipe. The first time the manager
is created on the server, it loads the required driver. Subsequently, the client
sends a request to the manager to create an instance of a specific ``MCDAQ`` device.

Once the ``MCDAQ`` device exists, clients connect to it directly (each device gets its own
pipe) and send data requests to control or read from the device. Multiple clients
may safely send requests to the same device. Each device reads or writes data in its
own thread.

For reading data from the device, there are generally two options:

#. Read the device and send back data to the client upon request.
#. The server contentiously reads data from the device and buffers it. It also
   contentiously sends the data it read to the client who initiated the read
   request.

Clients are responsible for closing the device when not needed anymore.

Data types
-------------

All data requests over the pipe start with common structs. Each device manages
the pipe used by clients of the device and defines the structs used by clients.
``cpl defs.h`` defines all the data types.
