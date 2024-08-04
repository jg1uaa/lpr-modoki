# lpr-modoki

---
## Description

An lpr-alike application to send printer image data.

**Not intended to be used with public or IPv6-based network.**


## Usage

```
# lpr-modoki
usage: lpr-modoki -a [ip address(dest)] -q [queue] -f [filename]
#
```

To describe IP address of target printer, print queue and image data with -a, -q and -f option respectively. As default, destination port is 515, source port is 731 and job number is random.

### Example

```
# lpr-modoki -a 127.0.0.1 -q queue -f file.pcl
```

## Limitation

- no support IPv6
- no support standard input
- no support large image file
- no support daemonize

No plan to fix them.

## License

WTFPL (http://www.wtfpl.net/)
