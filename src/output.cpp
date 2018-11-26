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

#include "output.hpp"

#include <cmath>

Output::Output(OGRLayer* input_layer, Options& options) :
    m_input_layer(input_layer),
    m_options(options) {
    OGRSpatialReference* input_srs = input_layer->GetSpatialRef();
    m_geographic_mode = input_srs->IsGeographic() || options.geographic;

    // set up output file
    OGRSFDriver* outDriver = OGRSFDriverRegistrar::GetRegistrar()->GetDriverByName(options.output_format.c_str());
    if (outDriver == NULL) {
        std::cerr << "ERROR: failed to load driver for " << options.output_format << '\n';
        exit(1);
    }
    m_out_data_source = outDriver->CreateDataSource(options.output_filename.c_str(),
            const_cast<char**>(options.dataset_creation_options.get()));
    if (m_out_data_source == NULL) {
        std::cerr << "ERROR: failed to create data source " << options.output_filename << '\n';
        exit(1);
    }
    m_output_layer = m_out_data_source->CreateLayer(
            input_layer->GetName(),
            input_srs,
            wkbUnknown,
            const_cast<char**>(options.layer_creation_options.get())
    );
    OGRFeatureDefn* input_feature_def = input_layer->GetLayerDefn();
    for (int i = 0; i < input_feature_def->GetFieldCount(); ++i) {
        OGRFieldDefn* field_def = input_feature_def->GetFieldDefn(i);
        if (m_output_layer->CreateField(field_def, TRUE) != OGRERR_NONE) {
            std::cerr << "Creating field " << field_def->GetNameRef() << " failed\n";
            exit(1);
        }
    }
}

Output::~Output() {
    OGRDataSource::DestroyDataSource(m_out_data_source);
}

/*static*/ double Output::deg_to_rad(const double degree) noexcept {
    return degree * (PI / 180.0);
}

/*static*/ double Output::distance(const double lon1, const double lat1, const double lon2, const double lat2) noexcept {
    if (m_geographic_mode) {
        // calculate distance on sphere
        double dx = EARTH_RADIUS_IN_METERS * deg_to_rad(lon2 - lon1);
        double dy = EARTH_RADIUS_IN_METERS * deg_to_rad(lat2 - lat1);
        return sqrt(dx * dx + dy * dy);
    }
    // calculate distance on plane
    return sqrt((lon2 - lon1) * (lon2 - lon1) + (lat2 - lat1) * (lat2 - lat1));
}

void Output::write_part(std::vector<double>& x_coords, std::vector<double>& y_coords, OGRFeature* feature) {
    OGRFeature* new_feature = OGRFeature::CreateFeature(feature->GetDefnRef());
    // copy fields
    for (int i = 0; i < feature->GetDefnRef()->GetFieldCount(); ++i) {
        new_feature->SetField(i, feature->GetRawFieldRef(i));
    }
    std::unique_ptr<OGRLineString> result {static_cast<OGRLineString*>(OGRGeometryFactory::createGeometry(wkbLineString))};
    // copy coordinates
    result->setNumPoints(static_cast<int>(x_coords.size()));
    result->setPoints(static_cast<int>(x_coords.size()), x_coords.data(), y_coords.data());
    new_feature->SetGeometry(result.get());
    if (m_output_layer->CreateFeature(new_feature) != OGRERR_NONE) {
        exit(1);
    }
    OGRFeature::DestroyFeature(new_feature);
    x_coords.at(0) = x_coords.back();
    y_coords.at(0) = y_coords.back();
    x_coords.erase(x_coords.begin() + 1, x_coords.end());
    y_coords.erase(y_coords.begin() + 1, y_coords.end());
}

void Output::split_linestring(OGRFeature* feature, OGRLineString* linestring) {
    if (skip_ring(linestring)) {
        return;
    }
    if (m_options.transaction_size == 0) {
        m_output_layer->StartTransaction();
    }
    double length = 0.0;
    std::vector<double> x_coords;
    std::vector<double> y_coords;
    for (int i = 0; i != linestring->getNumPoints(); ++i) {
        if (i > 0) {
            length += distance(linestring->getX(i - 1), linestring->getY(i - 1), linestring->getX(i), linestring->getY(i));
        }
        x_coords.push_back(linestring->getX(i));
        y_coords.push_back(linestring->getY(i));
        if (length > m_options.max_length) {
            write_part(x_coords, y_coords, feature);

            length = 0.0;
            ++m_transaction_count;
            if (m_transaction_count > m_options.transaction_size) {
                m_output_layer->CommitTransaction();
                m_output_layer->StartTransaction();
                m_transaction_count = 0;
            }
        }
    }
    if (x_coords.size() > 1) {
        write_part(x_coords, y_coords, feature);
    }
}

void Output::split_and_write_feature(OGRFeature* feature) {
    OGRGeometry* geom = feature->GetGeometryRef();
    if (geom->IsEmpty()) {
        return;
    }
    if (geom->getGeometryType() == wkbMultiLineString) {
        OGRMultiLineString* mls = static_cast<OGRMultiLineString*>(geom);
        for (int i = 0; i != mls->getNumGeometries(); ++i) {
            std::cerr << '.';
            split_linestring(feature, static_cast<OGRLineString*>(mls->getGeometryRef(i)));
        }
    } else if (geom->getGeometryType() == wkbLineString) {
        split_linestring(feature, static_cast<OGRLineString*>(geom));
    }
}

bool Output::skip_ring(OGRLineString* linestring) {
    if (linestring->get_IsClosed() && linestring->getNumPoints() > 5) {
        return false;
    }
    double length = 0.0;
    for (int i = 1; i != linestring->getNumPoints(); ++i) {
        length += distance(linestring->getX(i - 1), linestring->getY(i - 1), linestring->getX(i), linestring->getY(i));
    }
    return length < m_options.min_length;
}

void Output::run() {
    OGRFeature *f;
    m_input_layer->ResetReading();
    while ((f = m_input_layer->GetNextFeature()) != NULL) {
        split_and_write_feature(f);
        OGRFeature::DestroyFeature(f);
    }
}

void Output::finalize() {
    m_output_layer->CommitTransaction();
    m_output_layer->SyncToDisk();
}
