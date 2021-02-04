# sc0710
Linux driver for the Elgato 4k60 Pro Mk.2

This is a reverse engineering project. The goal is to bring support for the
Elgato 4k60 card to the linux platform.

The primary development platform for the project is Centos 7.5.1804 (Core), although
the driver is expected to work on multiple distributions.

# Background
Most of the investigation work is being done on Windows 10.
I'm instrumenting the hardware with debug wiring, identifying common
buses, sketching out a basic hardware digram, understanding the
individual components, monitoring hardware behavior and outlining
a plan for the linux implementation.

The project started Early Jan 2021. One month in, early feb, I understand enough of the
basic design, hardware layout, board debug points to start making an early
linux driver - enough to perform signal detection of the HDMI port and do some
basic hardware servicing.

All of my working notes, analyzer traces, daily journal notes will be
stored in this repository - as a single source for any interested viewers.

I'm maintaining a basic 'developer journal' so interested readers can follow along.
It's not my intension to do a "how to reverse engineer step-by-step" intro guide,
it's really to describe the process, show some of the tooling, highlight things that
worked and things that didn't work. I'm not writing an essay, its random utterances
that may help another developer on a similar project.

At this stage, everything is contained in master. We don't have any branches. As the
project progresses and the driver becomes usable, almost certainly, a new 'cleaner' repo
will emerge and users will not be expected to download this entire repo, with huge images,
analyzer traces, random notes - just to use the driver.

# Comments / Support

Email: stoth@kernellabs.com

# Content
* Project root - Driver source code.
* Docs - Daily journal, random notes.
* Traces - Various dump files taken from analyzers.
* Pics - Interesting or curious pictures I've taken during the process.



