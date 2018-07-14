#include "NNRay.hpp"
#include "FedUnit.hpp"
#include "StringCodeUtilities.hpp"

#include <vector>
#include <string>
#include <cmath>

// --- Feed Mu and Sigma

double NNRay::getFeedMu()
{
    double mu = 0.;
    for (std::vector<NetworkUnit *>::size_type i=0; i<_source.size(); ++i) {
        mu += _intensity[i] * _source[i]->getOutputMu();
    }
    return mu;
}


double NNRay::getFeedSigma()
{
    double var = 0.;
    for (std::vector<NetworkUnit *>::size_type i=0; i<_source.size(); ++i) {
        var += pow(_intensity[i] * _source[i]->getOutputSigma(), 2);
    }
    return sqrt(var);
}


// --- Betas

int NNRay::getNBeta(){return _intensity.size();}
double NNRay::getBeta(const int &i){return _intensity[i];}
void NNRay::setBeta(const int &i, const double &b){_intensity[i]=b;}

// --- Variational Parameters

int NNRay::getNVariationalParameters()
{
    return (_vp_id_shift > -1) ? _intensity.size() : 0;
}

int NNRay::getMaxVariationalParameterIndex()
{
    if (_vp_id_shift > -1) {
        return _vp_id_shift + getNVariationalParameters() - 1;
    }
    else return -1; // there are no vp in the whole feed
}

bool NNRay::setVariationalParameterValue(const int &id, const double &value){
    if (_vp_id_shift > -1) {
        if ( isVPIndexUsedInFeeder(id) ){
            _intensity[ id - _vp_id_shift ] = value;
            return true;
        }
    }
    return false;
}


bool NNRay::getVariationalParameterValue(const int &id, double &value){
    if (_vp_id_shift > -1) {
        if ( isVPIndexUsedInFeeder(id) ){
            value = _intensity[ id - _vp_id_shift ];
            return true;
        }
    }
    value = 0.;
    return false;
}


int NNRay::setVariationalParametersIndexes(const int &starting_index, const bool flag_add_vp){
    // Here we assign external vp indexes to internal indexes.
    // NOTE: The current method assumes, that no index larger than max_id,
    //       max_id = starting_index + source.size() - 1 ,
    //       may be in use FOR (and trivially IN) this ray.

    int idx_base = FeederInterface::setVariationalParametersIndexes(starting_index, flag_add_vp);

    _intensity_id.clear();

    if (flag_add_vp) {
        _vp_id_shift = idx_base;
        int idx=_vp_id_shift;
        for (std::vector<double>::size_type i=0; i<_intensity.size(); ++i) {
            _intensity_id.push_back(idx);
            idx++;
        }

        return idx;
    }
    else {
        return idx_base;
    }
}


// --- StringCode methods

std::string NNRay::getParams()
{
    std::string id_shift_str = FeederInterface::getParams();
    std::vector<std::string> beta_strs;

    for (std::vector<double>::size_type i=0; i<_intensity.size(); ++i) {
        beta_strs.push_back(composeParamCode("b"+std::to_string(i), _intensity[i]));
    }
    return composeCodes(id_shift_str, composeCodeList(beta_strs));
}


void NNRay::setParams(const std::string &params)
{
    FeederInterface::setParams(params);

    double beta;
    for (std::vector<double>::size_type i=0; i<_intensity.size(); ++i) {
        std::string str = readParamValue(params, "b"+std::to_string(i));
        if (setParamValue(str, beta)) this->setBeta(i, beta);
    }
}


// --- Computation


double NNRay::getFeed(){
    double feed = 0.;
    for (std::vector<NetworkUnit *>::size_type i=0; i<_source.size(); ++i){
        feed += _intensity[i]*_source[i]->getValue();
    }
    return feed;
}


double NNRay::getFirstDerivativeFeed(const int &i1d){
    double feed = 0.;
    for (std::vector<NetworkUnit *>::size_type i=1; i<_source.size(); ++i){
        feed += _intensity[i]*_source[i]->getFirstDerivativeValue(i1d);
    }

    return feed;
}


double NNRay::getSecondDerivativeFeed(const int &i2d){
    double feed = 0.;
    for (std::vector<NetworkUnit *>::size_type i=1; i<_source.size(); ++i){
        feed += _intensity[i]*_source[i]->getSecondDerivativeValue(i2d);
    }
    return feed;
}


double NNRay::getVariationalFirstDerivativeFeed(const int &iv1d){
    double feed = 0.;

    if (iv1d < _vp_id_shift+getNVariationalParameters()) {
        // if the variational parameter with index iv1d is in the ray add the following element
        if (iv1d >= _vp_id_shift) {
            feed += _source[ iv1d - _vp_id_shift ]->getValue();
        }
        else {
            // add source components
            for (size_t i=0; i<_map_index_to_sources[iv1d].size(); ++i) {
                feed += _intensity[_map_index_to_sources[iv1d][i]] * _source[_map_index_to_sources[iv1d][i]]->getVariationalFirstDerivativeValue(iv1d);
            }
        }
    }

    return feed;
}


double NNRay::getCrossFirstDerivativeFeed(const int &i1d, const int &iv1d){
    double feed = 0.;

    if (iv1d < _vp_id_shift+getNVariationalParameters()) {
        // if the variational parameter with index iv1d is in the ray add the following element
        if (iv1d >= _vp_id_shift) {
            feed += _source[ iv1d - _vp_id_shift ]->getFirstDerivativeValue(i1d);
        }
        else {
            // add source components
            for (size_t i=0; i<_map_index_to_sources[iv1d].size(); ++i) {
                feed += _intensity[_map_index_to_sources[iv1d][i]] * _source[_map_index_to_sources[iv1d][i]]->getCrossFirstDerivativeValue(i1d, iv1d);
            }
        }
    }

    return feed;
}


double NNRay::getCrossSecondDerivativeFeed(const int &i2d, const int &iv2d){
    double feed = 0.;

    if (iv2d < _vp_id_shift+getNVariationalParameters()) {
        // if the variational parameter with index iv2d is in the ray add the following element
        if (iv2d >= _vp_id_shift) {
            feed += _source[ iv2d - _vp_id_shift ]->getSecondDerivativeValue(i2d);
        }
        else {
            // add source components
            for (size_t i=0; i<_map_index_to_sources[iv2d].size(); ++i) {
                feed += _intensity[_map_index_to_sources[iv2d][i]] * _source[_map_index_to_sources[iv2d][i]]->getCrossSecondDerivativeValue(i2d, iv2d);
            }
        }
    }

    return feed;
}


// --- Constructor

NNRay::NNRay(NetworkLayer * nl): FeederInterface() {
    // target sigma to keep sum of weighted inputs in range [-4,4], assuming uniform distribution
    // sigma = 8/sqrt(12) = (b-a)/sqrt(12) * m^(1/2)
    const double bah = 4 * pow(nl->getNUnits(), -0.5); // (b-a)/2

    _rgen = std::mt19937_64(_rdev());
    _rd = std::uniform_real_distribution<double>(-bah,bah);

    for (int i=0; i<nl->getNUnits(); ++i){
        _source.push_back(nl->getUnit(i));
        _intensity.push_back(_rd(_rgen));
    }

    setVariationalParametersIndexes(nl->getMaxVariationalParameterIndex(), false); // per default we don't add betas as variational parameters
}

// --- Destructor

NNRay::~NNRay(){
    _intensity.clear();
    _intensity_id.clear();
}