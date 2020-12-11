# Link Demo

The basic link demo illustrates liveness and AIT (atomic information transfer).
It involves direct communication between two server/host machines.
AIT can be shown either via command-line tools,
or an interactive web page.

## Configuration

The configuration for a basic link demo involves many moving parts.

![Configuration Diagram](Link%20Demo%20Config%20(2020-12-09%20das).001.png)

Major components include:
  * An Ethernet connection between machines
  * eBPF/XDP programs running in each kernel
  * A privileged App Server to access the eBPF Map interface
  * A Web Server to handle browser requests
  * A client Web Browser to run the interactive visualization(s)

## Setup

Assuming a power-cycle reboot of the machines in the demo configuration,
the following steps are required to prepare for the demo.
