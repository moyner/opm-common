/*
  Copyright 2019 Equinor ASA.

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef ACTIONX_CONDITION_HPP
#define ACTIONX_CONDITION_HPP

#include <string>

namespace Opm {
namespace Action {

class Condition {
public:

enum class Logical {
    AND,
    OR,
    END
};

enum class Comparator {
    EQUAL,
    GREATER,
    LESS,
    GREATER_EQUAL,
    LESS_EQUAL,
    INVALID
};


    void add_token(const std::string& token);
    Condition(const std::string& quant);
    std::string cmp_string() const;


    std::string expression;
    std::string quantity;
    Logical logic = Logical::END;
    Comparator cmp = Comparator::INVALID;
};


}
}

#endif
