Crossfeed by Raw UDP Log
------------------------

cf-log project
==============

This project started because we have (hopefully) temporarily lost our crossfeed host!

What is crossfeed
=================

<a target="_blank" href="http://flightgear.org">FlightGear</a>, a 'free' flight simulation program has a
multiplayer network. That is the fgfs app can be started connected to one of the many multiplayer 
servers (fgms) around the world, and it will report your aircraft position, to the connected fgms.

One of the possibilities of the multiplayer server is that is will forward all udp packets 
received to a running crossfeed client.

This crossfeed client can set up a http service on a particular ports. We had this set up on
http://crossfeed.fgx.ch (now down), and you could receive a jason encoded list of pilots 
active on the multiplayer network.

The beauty of this simple feed is that is can tracked on a javascript map.

Here is a <a target="_blank" href="http://geoffair.org/fg/map-test2/map-test3.html">map</a> that 
does a similar thing using an 'alternate' active flight feed, using the 
<a target="_blank" href="http://mpserver15.flightgear.org/modules/content/index.php?id=4">mpserver15</a> 
tracker API. Of course it can only 'track' flights on multiplayer servers that are connected to the 
tracker.

So What is This?
================

Well for perhaps a day, crossfeed was configured to write the raw udp packets to a log 
file. A zip of this log can be downloaded from <a href="http://geoffair.org/tmp/cf_raw01.zip">here</a>

This cf-log project uses the content of that log to simulate a crossfeed feed.

As did the original crossfeed it sets up a http server, using the mongoose library, and will 
respond to json requests.

If running in a localhost, then that would be http://localhost:5555/flights.json

So this code will not be useful to many. It is just an example of using mongoose library 
to provide a simulate crossfeed http server, feeding up json (or xml) active flights.

The log will run for MANY hours, and when the end of the log is reached, all current 
flights are expired, and the log restarted from the beiginning.

Enjoy.

Geoff.
20140920

;eof
