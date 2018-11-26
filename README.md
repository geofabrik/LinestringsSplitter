# LineStringsSplitter

LineStringsSplitter is a small C++ programme to read linestrings from a shape file (or any other
format OGR/GDAL supports) and split them if they are longer than a given threshold.

Linestrings are only split at existing points and the threshold is currently not interpreted as a
hard limit. Instead, linestrings are splitted at the first point which is more than *n* metres away
from the last split location.


## License and Authors

This software was developed by Geofabrik GmbH. See the Git history for a full
list of contributors.

This software is licensed under the terms of GNU General Public License version
3 or newer. See [LICENSE.md](LICENSE.md) for the full legal text of the license.


## Dependencies

* C++11 compiler
* GDAL library (`libgdal-dev`)
* CMake (`cmake`)


## Building

```sh
mkdir build
cd build
cmake ..
make
```
