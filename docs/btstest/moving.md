
Introduction to btstest framework
---------------------------------

The purpose of this guide is to introduce other developers and community contributors to the `btstest`
testing framework by @theoreticalbts

Installing Python
-----------------

The `btstest` framework is implemented in Python 3.  Since the author uses Ubuntu LTS, it is most well-tested
with Python 3.4.0 on Ubuntu LTS, however some effort has been made to ease cross-platform compatibility.  In
particular, `btstest` Python extensions are used; the standard library is sufficient.

Please note that Python 3 is *not* backwards compatible with Python 2.  Running `btstest` in Python 2 is not
supported and likely will not work!  However, it is easy to have concurrent installations of Python 2 and
Python 3.

See https://www.python.org/downloads/ for Windows, or your distribution's package manager on Linux.
On Ubuntu and other Debian-based distributions, Python 3 can be installed with the command:

    sudo apt-get install python3

Writing a simple test
---------------------

Let us write a simple test.  In this test, we will obtain some XTS, register a public account, and then
ensure the blockchain recognizes the account.

All tests are run on a private testnet created specifically for the test.
