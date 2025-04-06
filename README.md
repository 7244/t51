# t51
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

t51 is network stress test utility which supposed to be more flexible than t50 in specific thingies.

## Features
- starts fast, doesnt uses libc.
- configureable packet count to send or infinite flood.
- able to random srcip.
- able to random src/dst port.
- configureable payload size. (doesnt exists in t50)

## Build

```sh
git clone --depth 1 https://github.com/7244/t51.git && \
cd t51

# if you dont have necessary libraries system wide
mkdir -p include && \
git clone --depth 1 https://github.com/7244/WITCH.git include/WITCH

make release
```

### Usage
- type `./t51 -h`

### Depends On
* https://github.com/7244/WITCH


## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
