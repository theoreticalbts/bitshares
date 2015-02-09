
The btstest framework
---------------------

`btstest` is a Python 3 framework for tests involving multiple clients.

Here are its goals:

- Simple test format; a copy-pasted output log is a valid test!
- Robust value expectation.  With regular expressions, you can store an unknown value such as a txid.
- Templating.  With templating, you can expect a value stored by a regular expression.
- Simple framework installation.  Does not require libraries outside Python 3 standard library, so virtualenv should not be needed unless your Python configuration is highly unusual.
- Turing-complete helper scripts.  From short inline calls to pre-created functions, to complete Python 3 modules, a test can include Python code inline or in its testenv directory.
- Framework hooks.  Want to check macro / micro balance [1] after every command without cluttering your test script?  Just register it as a pre/post command hook in the local testenv!
- Want to run every unit test, checking macro / micro balance after every command?  Just register it as a global pre/post command hook in the global testenv!
- Want to use multiple clients?  Unified or separate logs are supported!
- Tired of verbose and error-prone block syncing commands?  There's a single built-in command to advance time for all clients and wait until all clients see the next block!
- Want to simulate delayed or dropped network connections?  Script this behavior using the built-in proxy!
- Custom genesis block?  No problem!
- Need to obtain numerical results to check calculations?  All commands' data is automatically received in JSON format and parsed!
- Want to test interactions of clients running multiple versions of the software?  Each client may have a different executable specified in the testenv!

[1] The code maintains certain global ("macro") statistics such as total shares in existence of BTS or BitAssets.  And of course data on individual positions ("micro").
The intent is to maintain an invariant condition where the macro statistic is always equal to the sum of the micro positions.  Checking macro / micro balance simply
means verifying this invariant by iterating through micro entities (e.g. all balances), and comparing the sum obtained to the macro statistic (which is supposed to
be equal to the sum obtained, but might not be if there is a bug where some transactions, chain rewinding logic or whatever doesn't properly update the macro
statistic).

