# what

hisol is short for "https over ipmi sol".

It's a (more) secure version of ipmitool that does one thing only: connect you to the BMC's WebSocket-based SOL interface through HTTPS while verifying certs, implementing TLS, and doing all the things that let you sleep at night.

None of what it claims to do is actually accomplished yet: this is a work in progress.

# build

## Windows

```
scripts/build_windows.ps1
```
