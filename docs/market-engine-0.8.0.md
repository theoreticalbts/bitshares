
Document status
---------------

We are currently debating internally whether to move forward with the market engine refactoring described here.
The purpose of this document is to make a detailed proposal for the refactoring and describe why it is necessary.
The main argument against refactoring is a shortage of development resources.

Relative orders
---------------

As part of our market engine overhaul in preparation for 1.0, we are adding a new type of order, *relative orders*
which change with the price feed.  Relative orders will allow anyone to simply and easily make a market from the command
line (and eventually the wallet).  E.g. you can place an order which says "I'll sell my BitUSD at feed +5%" and another
order which says "I'll buy BitUSD at feed -5%."  Using some clever tweaks to the database and market engine,
in 0.8.0 the blockchain itself will execute relative orders with the same efficiency as regular ("absolute") bids and
asks.  So it is sustainable to charge people the same fee for relative orders as for absolute orders (a regular
transaction fee on placing or cancelling the order, and any overlap if it is matched).

Relative orders will also have a *limit price*; they will not execute at a worse price (below the limit price when selling,
above the limit price when buying).  This allows relative order writers some degree of protection against unexpected feed
movements.

The top matching principle
--------------------------

There are currently six order types:  Absolute bids, absolute asks, shorts, covers, relative bids, and relative asks.
They are listed in `order_type_enum` in `market_records.hpp`.

For each market, the market engine maintains a *book* consisting of all the orders in the market.  The book
is divided into two *sides*, the *bid side* and the *ask side*.  The GUI wallet allows
*flipping* a market between one of two *isomarkets*, the CLI wallet only supports a single isomarket which
reflects the internal blockchain-level organization of the market.  In the internal isomarket, shorts,
relative bids, and absolute bids are bid-side, while covers, relative asks and absolute asks are ask-side.

Each *side* is further divided into multiple *lists*.  Each list contains only one type of orders, and each list
maintains a database index (or primary key) sorting according to how favorable the offer is to the counterparty.
The *top-of-list* is the order in a list which is most favorable to the counterparty (ties are broken in a
deterministic way).  Our lists obey the *top matching principle*:

- Every order which matches any order in a list must match the top-of-list.

The naive order matching algorithm
----------------------------------

The *naive order matching algorithm* was the first matching algorithm we considered for 0.8.0, which was the most
obvious evolution of the live (0.5.x) order matching algorithm.  Naive matching occurs as follows:

- Divide the six order types into six lists.
- Merge the three top-of-lists on each side to get a top-of-book order for each side.
- Match the two top-of-book orders against each other.
- If the top-of-book orders didn't match, terminate since no more matches are possible.
- If there was a match, remove whichever order(s) was (were) filled.
- Update the top-of-list(s) for the affected order type.
- Repeat, starting from "Merge the three top-of-lists on each side..."

Problems with naive matching
----------------------------

Upon a combination of closer investigation, intensive testing and lots of back-and-forth communication
in Github tickets and in person within the core developer team, we have discovered the following
desired behaviors which are *fundamentally incompatible* with naive matching:

- Shorts and relative orders can become sessile (at their limit price) or mobile (away from their limit price and moving with the feed)
- Cover orders can become active or inactive when the feed crosses the margin call price
- Cover orders can become expired after a certain amount of time
- Margin calls (cover orders activated by virtue of price feed movement) should take priority over all other ask-side orders
- Expired cover orders (cover orders activated by virtue of the passage of time) take priority over all ask-side orders except margin calls

The latent update problem
-------------------------

Each order has 1-3 *modes of execution*.  Each mode of execution uses different matching criteria, and an order's mode of execution can
change depending on external chain state.

- Absolute bids and asks have a single mode of execution.
- Shorts have two modes of execution, *sessile* (at the limit price) or *mobile* (at the feed, competing on interest).
- Relative orders have two modes of execution, *sessile* (at the limit price) or *mobile* (based on a percentage of the feed price).
- Cover orders have three modes of execution:  *called* (at the feed price when the margin call price has been reached), *expired* (at the feed price
when the order is expired), and *inoperative* (when neither of the previous conditions is met).

Index notes
-----------

This is some quick notes on what indexes are necessary to maintain.  This list of indexes is not final!

    shorts:  mobile

    _up : lower value matches first
    _down : higher value matches first

    sessile short:  (limit_price_down, owner_up, rp_down, interest_down)
    mobile short:   (rp_down, interest_down, owner_up, limit_price_down)
    sessile r.b.:   (limit_price_down, owner_up)
    mobile r.b.:    (rel_price_down, owner_up)

    cover_called:   (call_price_up, owner_up) 
    cover_expd:     (call_price_up, owner_up)
    cover_noop:
    sessile r.a.:   (limit_price_up, owner_up)
    mobile r.a.:    (rel_price_up, owner_up)


    short needs indexed by limit price
    relative short needs to be indexed by feed_stop
    rb / ra needs to be indexed by limit price
    cover needs to be indexed by call price, exp. date

Orders and offers
-----------------

Another problem which has become obvious is that we have a giant `if` statement with
many different *pairs of order types*!  Obviously we need to consider `a*b` cases if
we have `a` ask-side order types and `b` bid-side order types.  Which may grow even
further in the future (e.g. if we implement LMSR prediction markets)!

This leads to much logic duplication and is a "code smell."

Each order can be transformed into an *offer* which is the proposed trade
implied by the order and the current chain state.  For absolute bid/ask, the order
and offer are identical.  For a short or a relative bid/ask, the offer simply fills
in the bid and ask prices and quantities based on the current feed price.  For a cover,
the offer is filled in based on whether the feed has crossed the cover price (if margin
called), the expiration date (if expired), or nulled (if the order is inactive).

The offers are not stored in the database.  Instead they only exist in the market engine;
the top-of-list offers are constructed in memory by the market engine.

Instead of matching and filling orders, we match and fill offers.  This allows things
like the YGWYAF overlapping fee logic and quantity computation for partial matches to
exist on a single code path.

Of course after the offers are matched and filled, we must post-process the filled order
to do type- or mode-specific handling such as updating the original order quantity,
creating a cover order from a matched short, etc.

From a software engineering standpoint, one of `a` types of pre-processing, followed by
common logic `c`, followed by one of `b` types of post-processing should only take `a+b`
different conditional sections and a single copy of the common `c`.  OTOH the
pre-0.8 implementation which sort of organically grew from 0.5.x code, had about
`a*b` different conditional sections and `a*b` slightly tweaked copies of `c`.


