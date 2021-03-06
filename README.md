# Overview
tty-mux is a generic tool to inspect messages sent from a tty device.
It reads all the messages and sent it to another pesudo tty device and
tcp clients optionly.

This features enables device administrators to monitor messages from
tty device, and take some actions when messages interested happen,
without affect original processing flow by other process.

Administrators can even inject some additional events when something
interested happen.

# License
tty-mux complies Apache License Version 2.0.

# Compiling
Compile with Android NDK android-ndk-r16b. Developers can also port
it to other platforms. This software uses POXSIX interface and
can port to any unix-like platform.


