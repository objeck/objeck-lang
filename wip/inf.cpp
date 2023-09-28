#include <limits>
#include <iostream>

int main() {
    double Inf = std::numeric_limits<double>::infinity();  
    double negative_Inf= Inf*-1;        

    std::cout << "The value of Positive infinity is : " << Inf << std::endl;
    std::cout << "The value of Negative infinity is : " << negative_Inf << std::endl;

    return 0;
}