#include <iostream>
#include <bitset>
#include <algorithm>
#include <cmath>

using namespace std;

string binary_addition(string x, string y) {
  
  int sign_x = stoi(x.substr(0, 1));
  int exp_x = stoi(x.substr(1, 8), nullptr, 2);
  int mant_x = stoi(x.substr(9), nullptr, 2);
  int sign_y = stoi(y.substr(0, 1));
  int exp_y = stoi(y.substr(1, 8), nullptr, 2);
  int mant_y = stoi(y.substr(9), nullptr, 2);
  
  mant_x += pow(2, 23);
  mant_y += pow(2, 23);
  
  int exp;
  if (exp_x > exp_y) {
    mant_y = mant_y >> (exp_x - exp_y);
    exp = exp_x;
  } else if (exp_x < exp_y) {
    mant_x = mant_x >> (exp_y - exp_x);
    exp = exp_y;
  } else {
    exp = exp_x;
  }
  
  int sign, mant;
  if (sign_x == sign_y) {
    sign = sign_x;
    mant = mant_x + mant_y;
  } else {
    sign = (mant_x > mant_y) ? sign_x : sign_y;
    mant = abs(mant_x - mant_y);
  }
  
  while (mant >= pow(2, 24)) {
    mant = mant >> 1;
    exp += 1;
  }
  while (mant < pow(2, 23)) {
    mant = mant << 1;
    exp -= 1;
  }
  
  mant -= pow(2, 23);
  
  string result = to_string(sign) + bitset<8>(exp).to_string() + bitset<23>(mant).to_string();
  
  return result;
}

int main() {
  string x, y;
  cout << "Enter the first binary floating point number: ";
  cin >> x;
  cout << "Enter the second binary floating point number: ";
  cin >> y;
  
  if (x.length() != 32 || y.length() != 32 || !all_of(x.begin(), x.end(), ::isdigit) || !all_of(y.begin(), y.end(), ::isdigit)) {
    cout << "Invalid inputs. Please enter 32-bit binary strings." << endl;
  } else {
    string result = binary_addition(x, y);
    cout << "The result of adding " << x << " and " << y << " is " << result << endl;
  }
  
  return 0;
}