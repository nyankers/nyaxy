Setup
-----

Use GNU make, e.g. `make` or `gmake`.


Running
-------

Start by running `nyaxy` with a port to listen on:

```
./nyaxy <port>
```

The first line from any incoming communications (up to the first `"\r\n"`
pair) must be in the form of `<host>:<port>` e.g. `google.com:80`. That will
determine what `nyaxy` should connect the client to.


Security
--------

There is none built-in. You'll want to hide whatever port you open behind a
firewall. For example, you could use `ssh` port-forwarding to access the
program.


Bugs
----

This was written in a couple hours, so very likely many.
