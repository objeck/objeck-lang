#include <string>
#include <sstream>
#include <iostream>
#include <map>
#include <vector>

using namespace std;

string LongToString(long v) {
  ostringstream str;
  str << v;
  return str.str();
}
