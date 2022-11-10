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
#include <iostream>

#include <cmath>

Output::Output(OGRLayer* input_layer, Options& options) :
    m_input_layer(input_layer),
    m_options(options),
    m_input_srs(m_input_layer->GetSpatialRef()),
    m_out_data_source() {
    init();
}

void Output::init() {
    m_geographic_mode = m_input_srs->IsGeographic() || m_options.geographic;

    // set up output file
#if GDAL_VERSION_MAJOR >= 2
    gdal_driver_type* out_driver = GetGDALDriverManager()->GetDriverByName(m_options.output_format.c_str());
#else
    gdal_driver_type* out_driver = OGRSFDriverRegistrar::GetRegistrar()->GetDriverByName(m_options.output_format.c_str());
#endif
    if (out_driver == NULL) {
        std::cerr << "ERROR: failed to load driver for " << m_options.output_format << '\n';
        exit(1);
    }
#if GDAL_VERSION_MAJOR >= 2
    m_out_data_source.reset(out_driver->Create(m_options.output_filename.c_str(), 0, 0, 0, GDT_Unknown,
            const_cast<char**>(m_options.dataset_creation_options.get())));
#else
    m_out_data_source = outDriver->CreateDataSource(m_options.output_filename.c_str(),
            const_cast<char**>(m_options.dataset_creation_options.get()));
#endif
    if (m_out_data_source == NULL) {
        std::cerr << "ERROR: failed to create data source " << m_options.output_filename << '\n';
        exit(1);
    }
    m_output_layer = m_out_data_source->CreateLayer(
            m_input_layer->GetName(),
            m_input_srs,
            wkbLineString,
            const_cast<char**>(m_options.layer_creation_options.get())
    );
    OGRFeatureDefn* input_feature_def = m_input_layer->GetLayerDefn();
    for (int i = 0; i < input_feature_def->GetFieldCount(); ++i) {
        OGRFieldDefn* field_def = input_feature_def->GetFieldDefn(i);
        if (m_output_layer->CreateField(field_def, TRUE) != OGRERR_NONE) {
            std::cerr << "Creating field " << field_def->GetNameRef() << " failed\n";
            exit(1);
        }
    }
}

Output::~Output() {
#if GDAL_VERSION_MAJOR < 2
    OGRDataSource::DestroyDataSource(m_out_data_source);
#endif
}

/*static*/ double Output::deg_to_rad(const double degree) noexcept {
    return degree * (PI / 180.0);
}

double Output::distance(const double lon1, const double lat1, const double lon2, const double lat2) noexcept {
    if (m_geographic_mode) {
        // calculate distance on sphere
        double dx = EARTH_RADIUS_IN_METERS * deg_to_rad(lon2 - lon1);
        double dy = EARTH_RADIUS_IN_METERS * deg_to_rad(lat2 - lat1);
        return sqrt(dx * dx + dy * dy);
    }
    // calculate distance on plane
    return sqrt((lon2 - lon1) * (lon2 - lon1) + (lat2 - lat1) * (lat2 - lat1));
}

void Output::write_part(std::vector<double>&& x_coords, std::vector<double>&& y_coords, OGRFeature* feature) {
    OGRFeature* new_feature = OGRFeature::CreateFeature(feature->GetDefnRef());
    // copy fields
    for (int i = 0; i < feature->GetDefnRef()->GetFieldCount(); ++i) {
        new_feature->SetField(i, feature->GetRawFieldRef(i));
    }
    std::unique_ptr<OGRLineString> result {static_cast<OGRLineString*>(OGRGeometryFactory::createGeometry(wkbLineString))};
    result->assignSpatialReference(m_input_srs);
    // copy coordinates
    result->setNumPoints(static_cast<int>(x_coords.size()));
    result->setPoints(static_cast<int>(x_coords.size()), x_coords.data(), y_coords.data());
    new_feature->SetGeometryDirectly(result.release());
    if (m_output_layer->CreateFeature(new_feature) != OGRERR_NONE) {
        std::cerr << "ERROR during writing a feature\n";
        exit(1);
    }
    OGRFeature::DestroyFeature(new_feature);
}

void Output::split_linestring(OGRFeature* feature, OGRLineString* linestring) {
    if (skip_ring(linestring)) {
        return;
    }
    if (m_options.transaction_size == 0) {
        if (m_output_layer->StartTransaction() != OGRERR_NONE) {
            std::cerr << "Failed to start transaction in output layer.\n";
            exit(1);
        }
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
            write_part(std::move(x_coords), std::move(y_coords), feature);
            x_coords = std::vector<double>();
            y_coords = std::vector<double>();
            x_coords.push_back(linestring->getX(i));
            y_coords.push_back(linestring->getY(i));

            length = 0.0;
            ++m_transaction_count;
            if (m_transaction_count > m_options.transaction_size) {
                if (m_output_layer->CommitTransaction() != OGRERR_NONE && m_output_layer->StartTransaction() != OGRERR_NONE) {
                    std::cerr << "Failed to start a new transaction in output layer.\n";
                    exit(1);
                }
                m_transaction_count = 0;
            }
        }
    }
    if (x_coords.size() > 1) {
        write_part(std::move(x_coords), std::move(y_coords), feature);
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
    if (m_output_layer->CommitTransaction() != OGRERR_NONE) {
        std::cerr << "Failed to commit transaction in output layer.\n";
        exit(1);
    }
    m_output_layer->SyncToDisk();
}
