#include <limits>
#include <iostream>

int main() {
    const double inf = std::numeric_limits<double>::infinity();  
    const double neg_inf= inf*-1;
    const double nan = std::numeric_limits<double>::quiet_NaN();  
    
    std::cout << "The value of nan is : " << nan << std::endl;
    std::cout << "The value of positive infinity is : " << inf << std::endl;
    std::cout << "The value of negative infinity is : " << neg_inf << std::endl;
    
    return 0;
}