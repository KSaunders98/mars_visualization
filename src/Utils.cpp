#include "Utils.h"

#include <cmath>

using namespace Aftr;

using namespace web::http;
using namespace web::http::client;

static const std::string API_URL = "http://192.168.1.110:3000/";
static const std::string API_ELEV_URL = API_URL + "elevation";
static const std::string API_IMG_URL = API_URL + "imagery";

constexpr double MARS_SEMIMAJOR_AXIS = 3396190.0; // in meters
constexpr double MARS_RECIPROCAL_FLATTENING = 0.0058860075555254854;

VectorD Aftr::toMars2000FromCartesian(const VectorD& p, double scale)
{
    double a, f, b, e2, ep2, r2, r, E2, F, G, c, s, P, Q, ro, tmp, U, V, zo, h, phi, lambda;
    double Z = p.z;
    double X = p.x;
    double Y = p.y;
    //phi - latitude
    //lamba - longitude
    //h - elevation

    a = MARS_SEMIMAJOR_AXIS * scale;
    f = MARS_RECIPROCAL_FLATTENING;
    b = a * (1 - f); // semi-minor axis (3376200 meters)

    e2 = 2 * f - f * f;// first eccentricity squared
    ep2 = f * (2 - f) / (std::pow((1 - f), 2.0f)); // second eccentricity squared

    r2 = X * X + Y * Y;
    r = std::sqrt(r2);
    E2 = a * a - b * b;
    F = 54 * b * b * Z * Z;
    G = r2 + (1 - e2) * Z * Z - e2 * E2;
    c = (e2 * e2 * F * r2) / (G * G * G);
    s = std::pow(1 + c + std::sqrt(c * c + 2 * c), (1 / 3));
    P = F / (3 * std::pow(s + 1 / s + 1, 2.0f) * G * G);
    Q = std::sqrt(1 + 2 * e2 * e2 * P);
    ro = -(e2 * P * r) / (1 + Q) + std::sqrt((a * a / 2) * (1 + 1 / Q) - ((1 - e2) * P * Z * Z) / (Q * (1 + Q)) - P * r2 / 2);
    tmp = std::pow(r - e2 * ro, 2.0f);
    U = std::sqrt(tmp + Z * Z);
    V = std::sqrt(tmp + (1 - e2) * Z * Z);
    zo = (b * b * Z) / (a * V);

    h = U * (1 - (b * b) / (a * V));
    phi = std::atan2(Z + ep2 * zo, r);
    lambda = std::atan2(Y, X);

    phi *= Aftr::RADtoDEGd;
    lambda *= Aftr::RADtoDEGd;

    return VectorD(phi, lambda, h);
}

VectorD Aftr::toCartesianFromMars2000(const VectorD& p, double scale)
{
    double a = MARS_SEMIMAJOR_AXIS * scale;
    double e2 = 2 * MARS_RECIPROCAL_FLATTENING - MARS_RECIPROCAL_FLATTENING * MARS_RECIPROCAL_FLATTENING;
    double elev = p.z * scale;

    double latRad = p.x * Aftr::DEGtoRADd;
    double lonRad = p.y * Aftr::DEGtoRADd;

    double sinLatRad = std::sin(latRad);
    double e2sinLatSq = e2 * (sinLatRad * sinLatRad);

    double rn = a / std::sqrt(1 - e2sinLatSq);
    double R = (rn + elev) * std::cos(latRad);

    VectorD cartVec;
    cartVec.x = R * std::cos(lonRad);
    cartVec.y = R * std::sin(lonRad);
    cartVec.z = (rn * (1 - e2) + elev) * std::sin(latRad);

    return cartVec;
}

uint32_t Aftr::getPatchIndexFromMars2000(const VectorD& p)
{
    uint32_t x = static_cast<uint32_t>(p.y + 180.0);
    uint32_t y = static_cast<uint32_t>(90.0 - p.x);

    return x + y * 360;
}

VectorD Aftr::getMars2000FromPatchIndex(uint32_t index)
{
    uint32_t x = index % 360;
    uint32_t y = index / 360;
    double theta = static_cast<double>(x) - 180.0;
    double phi = 90.0 - static_cast<double>(y);

    return VectorD(phi, theta, 0.0);
}

bool Aftr::makeGetRequest(const std::string base_uri, uri_builder& uri, std::vector<unsigned char>& result)
{
    http_client client(utility::conversions::utf8_to_utf16(base_uri));
    http_response response;
    try {
        response = client.request(methods::GET, uri.to_string()).get();
    }
    catch (...) {
        std::cerr << "Unable to make get request: " << uri.to_string().c_str() << std::endl;
        return false;
    }

    if (response.status_code() != status_codes::OK) {
        std::cerr << "Get request failed: " << uri.to_string().c_str()
            << "\n\tStatus Code: " << response.status_code() << std::endl;
        return false;
    }

    std::vector<unsigned char> data;
    try {
        data = response.extract_vector().get();
    }
    catch (...) {
        std::cerr << "Get request failed: " << uri.to_string().c_str()
            << "\n\tUnable to get data from response" << std::endl;
        return false;
    }

    // copy data into result
    result.resize(data.size());
    std::copy(data.begin(), data.end(), result.begin());

    return true;
}

bool Aftr::loadElevation(uint32_t id, std::vector<int16_t>& data)
{
    uri_builder builder{};
    builder.append_query(L"id", id);

    std::vector<unsigned char> result;
    bool success = makeGetRequest(API_ELEV_URL, builder, result);
    if (!success) {
        std::cerr << "Failed to load elevation data for tile id: " << id << std::endl;
        return false;
    }

    size_t expected_size = PATCH_RESOLUTION * PATCH_RESOLUTION * sizeof(int16_t);
    if (result.size() != expected_size) {
        std::cerr << "Unable to fetch elevation data for tile id: " << id
            << "\n\tIncorrect response size: " << result.size() << " bytes (expected " << expected_size << " bytes)" << std::endl;
        return false;
    }

    data.resize(PATCH_RESOLUTION * PATCH_RESOLUTION);
    for (size_t i = 0; i < result.size(); i += 2) {
        // bytes are in big-endian int16 format
        int16_t e = static_cast<int16_t>(result[i]) << 8 | static_cast<int16_t>(result[i + 1]);
        data[i / 2] = e;
    }

    return true;
}

bool Aftr::loadImagery(uint32_t id, std::vector<GLubyte>& data)
{
    uri_builder builder{};
    builder.append_query(L"id", id);

    std::vector<unsigned char> result;
    bool success = makeGetRequest(API_IMG_URL, builder, result);
    if (!success) {
        std::cerr << "Failed to load imagery data for tile id: " << id << std::endl;
        return false;
    }

    size_t expected_size = PATCH_RESOLUTION * PATCH_RESOLUTION * 3 * sizeof(GLubyte);
    if (result.size() != expected_size) {
        std::cerr << "Unable to fetch imagery data for tile id: " << id
            << "\n\tIncorrect response size: " << result.size() << " bytes (expected " << expected_size << " bytes)" << std::endl;
        return false;
    }

    data.resize(PATCH_RESOLUTION * PATCH_RESOLUTION * 3);
    std::copy(result.begin(), result.end(), data.begin());

    return true;
}