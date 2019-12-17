/*
 *  © 2018 Geofabrik GmbH
 *
 *  This file is part of LinestringsSplitter.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 3
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <getopt.h>

#include <algorithm>
#include <cstring>
#include <iostream>

#include "output.hpp"


void print_help(char* arg0) {
    std::cerr << "Usage: " << arg0 << " [OPTIONS] INFILE OUTFILE\n" \
              << "Options:\n" \
              << "  -h, --help           This help message.\n" \
              << "  -f, --format         Output format (default: SQlite)\n" \
              << "  --dsco KEY=VALUE     Dataset creation options for output format\n" \
              << "  --geographic         Treat coordinates as geographic (lat/long) and\n" \
              << "                       calculate distances on a sphere. This option is\n" \
              << "                       not required if the coordinate system is recognized\n" \
              << "                       correctly.\n" \
              << "  --gt NUMBER          Group NUMBER features per transaction\n" \
              << "  --lco  KEY=VALUE     Options for output format\n" \
              << "  -m NUM, --min-length NUM    minimum length in meter for circular linestrings with 5 points\n" \
              << "  -M NUM, --max-length NUM    maximum length of a linestring\n";
}


std::string get_directory(std::string& path) {
    size_t last_slash = path.find_last_of("/");
    if (last_slash == std::string::npos) {
        return ".";
    } else {
        return path.substr(0, last_slash+1);
    }
}


std::string get_filename(std::string& path) noexcept {
    size_t begin = path.find_last_of("/");
    // look for .shp suffix
    size_t end = path.rfind(".shp");
    if (begin == std::string::npos) {
        std::cerr << "ERROR: Output path is a directory but should be a file.\n";
        exit(1);
    } else {
        return path.substr(begin+1, end);
    }
}

std::vector<std::string> get_options_vector(const char* str) {
    std::vector<std::string> options;
    const char* last_pos = str;
    const char* pos = str;
    const char* str_end = std::strchr(str, '\0');;
    while (pos != str_end) {
        pos = std::strchr(pos + 1, ',');
        if (!pos) {
            pos = str_end;
        }
        if (last_pos == str) {
            options.emplace_back(str, pos - str);
        } else {
            options.emplace_back(last_pos + 1, pos - last_pos - 1);
        }
        last_pos = pos;
    }
    return options;
}

std::unique_ptr<const char*[]> options_list(const std::vector<std::string>& options) {
    std::unique_ptr<const char*[]> ptrs {new const char*[options.size() + 1]};
    std::transform(options.begin(), options.end(), ptrs.get(), [&](const std::string& s) {
        return s.data();
    });
    ptrs[options.size()] = nullptr;
    return ptrs;
}


int main(int argc, char* argv[]) {
    // parse command line arguments

    constexpr int dsco_option = 200;
    constexpr int gt_option = 201;
    constexpr int lco_optoin = 202;

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"format", required_argument, 0, 'f'},
        {"dsco", required_argument, 0, dsco_option},
        {"gt", required_argument, 0, gt_option},
        {"lco", required_argument, 0, lco_optoin},
        {"min-length", required_argument, 0, 'm'},
        {"max-length", required_argument, 0, 'M'},
        {0, 0, 0, 0}
    };
    Options options;
    std::vector<std::string> dsco_vector;
    std::unique_ptr<const char*[]> dsco;
    std::vector<std::string> lco_vector;
    std::unique_ptr<const char*[]> lco;
    while (true) {
        int c = getopt_long(argc, argv, "hf:m:M:", long_options, 0);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'h':
            print_help(argv[0]);
            exit(1);
            break;
        case 'f':
            options.output_format = optarg;
            break;
        case dsco_option:
            dsco_vector = get_options_vector(optarg);
            options.dataset_creation_options = options_list(dsco_vector);
            break;
        case lco_optoin:
            lco_vector = get_options_vector(optarg);
            options.layer_creation_options = options_list(lco_vector);
            break;
        case gt_option:
            options.transaction_size = std::atoi(optarg);
            break;
        case 'm':
            options.min_length = std::atoi(optarg);
            break;
        case 'M':
            options.max_length = std::atoi(optarg);
            break;
        default:
            std::cerr << "ERROR: unknown command line option\n";
            print_help(argv[0]);
            exit(1);
            break;
        }
    }
    int remaining_args = argc - optind;
    if (remaining_args != 2) {
        std::cerr << "ERROR: two positional arguments requried\n";
        print_help(argv[0]);
        exit(1);
    }
    std::string input_filename =  argv[optind];
    options.output_filename = argv[optind+1];

    // set up input file
#if GDAL_VERSION_MAJOR >= 2
    GDALAllRegister();
    gdal_dataset_type::pointer input_data_source {static_cast<gdal_dataset_type::pointer>(GDALOpenEx(input_filename.c_str(), GDAL_OF_VECTOR, NULL, NULL, NULL))};
#else
    OGRRegisterAll();
    gdal_dataset_type* input_data_source = OGRSFDriverRegistrar::Open(input_filename.c_str());
#endif

    if (input_data_source == nullptr) {
        std::cerr << "ERROR: Open of " << input_filename << " failed.\n";
        exit(1);
    }
    // use the first layer only
    OGRLayer* input_layer = input_data_source->GetLayer(0);
    if (input_layer == nullptr) {
        std::cerr << "ERROR: no data layer in " << input_filename << '\n';
        exit(1);
    }
    if (input_layer->GetGeomType() != wkbLineString && input_layer->GetGeomType() != wkbMultiLineString) {
        std::cerr << "ERROR: cannot work with files containing other geometry types than linestring and multilinestring\n";
        exit(1);
    }

    if (input_layer->GetSpatialRef()) std::cerr << "input has spatial ref\n";

    Output output {input_layer, options};
    output.run();
    output.finalize();
#if GDAL_VERSION_MAJOR >= 2
    GDALClose(static_cast<GDALDatasetH>(input_data_source));
#else
    OGRDataSource::DestroyDataSource(input_data_source);
    OGRCleanupAll();
#endif
}

