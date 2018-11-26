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

#ifndef OUTPUT_LAYER_HPP_
#define OUTPUT_LAYER_HPP_

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <ogr_api.h>
#include <ogrsf_frmts.h>

struct Options {
    std::string output_filename;

    std::string output_format = "ESRI Shapefile";

    int transaction_size = 1000;

    bool geographic = false;

    double min_length = 200;

    double max_length = 2000;

    std::unique_ptr<const char*[]> dataset_creation_options;

    std::unique_ptr<const char*[]> layer_creation_options;
};

class Output {
private:
    OGRLayer* m_input_layer;

    Options& m_options;

    bool m_geographic_mode;

    OGRDataSource* m_out_data_source;

    OGRLayer* m_output_layer;

    int m_transaction_count = 0;

    static constexpr double PI = 3.14159265358979323846;

    static constexpr const double EARTH_RADIUS_IN_METERS = 6372797.560856;

    static double deg_to_rad(const double degree) noexcept;

    static double distance(const double lon1, const double lat1, const double lon2, const double lat2) noexcept;

    void write_part(std::vector<double>& x_coords, std::vector<double>& y_coords, OGRFeature* feature);

    void split_linestring(OGRFeature* feature, OGRLineString* linestring);

    void split_and_write_feature(OGRFeature* feature);

    /**
     * Check if a linestring should be skipped.
     */
    bool skip_ring(OGRLineString* geometry);

public:
    Output(OGRLayer* input_layer, Options& options);

    ~Output();

    void run();

    void finalize();
};



#endif /* OUTPUT_LAYER_HPP_ */
