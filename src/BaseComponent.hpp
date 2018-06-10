#ifndef BASE_COMPONENT
#define BASE_COMPONENT

#include <string>

// Base class for all network components
// Mainly used to manage the string code methods
class BaseComponent
{
protected:

public:

    // Every BaseComponent with params and/or members should implement a constructor which uses a params and/or memberFullCodes string as one of the arguments

    virtual ~BaseComponent(){};

    // virtual string code getters, to be extended by child
    // Compositions patterns for full/tree codes must be:
    // id ( params ) { member1_id ( member1_params ) { member1_member1_id ... } member2_id ( member2_params ) { ... } ... }
    virtual std::string getIdCode() = 0; // return unique (at least within class) identifier for component type
    virtual std::string getClassIdCode() = 0; // Usually should be set only by the direct child of BaseComponent
    virtual std::string getParams(){return "";} // return parameter string
    virtual std::string getMemberTreeIdCode(){return "";} // return TreeIdCodes of added BaseComponent members
    virtual std::string getMemberTreeFullCode(){return "";} // return TreeFullCodes of added BaseComponent members


    // string code composers
    std::string getFullCode(){return this->getIdCode() + " ( " + this->getParams() + " )";} //return id + params
    std::string getTreeIdCode(){return this->getIdCode() + " { " + this->getMemberTreeIdCode() + " }";} //return id + member ids, recursively
    std::string getTreeFullCode(){return this->getFullCode() + " { " + this->getMemberTreeFullCode() + " }";} // return id+params + member ids+params, recursively

    // set by string code
    virtual void setParams(const std::string &params){} // set params of this by params string
    virtual void setMemberParams(const std::string &memberFullCodes){} // set params of all members by memberFullCodes string
};



#endif