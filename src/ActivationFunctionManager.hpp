#ifndef ACTIVATION_FUNCTION_MANAGER
#define ACTIVATION_FUNCTION_MANAGER


#include "ActivationFunctionInterface.hpp"
#include "IdentityActivationFunction.hpp"
#include "LogisticActivationFunction.hpp"
#include "GaussianActivationFunction.hpp"
#include "ReLUActivationFunction.hpp"
#include "SELUActivationFunction.hpp"
#include "TanSigmoidActivationFunction.hpp"
#include "SineActivationFunction.hpp"

#include <string>


namespace std_actf{

    extern IdentityActivationFunction id_actf;
    extern LogisticActivationFunction lgs_actf;
    extern GaussianActivationFunction gss_actf;
    extern ReLUActivationFunction relu_actf;
    extern SELUActivationFunction selu_actf;
    extern TanSigmoidActivationFunction tans_actf;
    extern SineActivationFunction sin_actf;

    ActivationFunctionInterface * provideActivationFunction(const std::string idcode);
}



#endif
