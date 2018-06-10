#ifndef NN_UNIT
#define NN_UNIT

#include "FedNetworkUnit.hpp"
#include "ActivationFunctionInterface.hpp"
#include "NetworkUnitFeederInterface.hpp"

#include <stdexcept>

// Unit of an Artificial Neural Network
class NNUnit: public FedNetworkUnit
{
protected:

    // Activation Function of the unit
    // A function that calculates the output value from the input value (protovalue)
    ActivationFunctionInterface * _actf; // activation function

public:
    // Constructor and destructor
    NNUnit(ActivationFunctionInterface * actf, NetworkUnitFeederInterface * feeder = NULL) : FedNetworkUnit(feeder) {if (actf) _actf = actf; else throw std::invalid_argument("NNUnit(): the parameter 'actf' was not valid");}
    virtual ~NNUnit(){ delete _actf; }

    // string code getters
    virtual std::string getIdCode(){return "nnu";} // return identifier for unit type

    virtual std::string getMemberIdCodes(){return FedNetworkUnit::getMemberIdCodes() + " " + _actf->getIdCode();} // append actf IdCodes
    virtual std::string getMemberFullCodes(){return FedNetworkUnit::getMemberFullCodes() + " " + _actf->getFullCode();} // append actf IdCodes + Params

    // Setters
    void setActivationFunction(ActivationFunctionInterface * actf){delete _actf; if (actf) _actf=actf; else throw std::invalid_argument("NNUnit::setActivationFunction(): the parameter 'actf' was not valid");}

    // Getters
    ActivationFunctionInterface * getActivationFunction(){return _actf;}

    // Computation
    void computeOutput();
};


#endif
