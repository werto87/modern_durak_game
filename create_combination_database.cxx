//
// Created by walde on 2/11/24.
//
#include "modern_durak_game/util/util.hxx"
#include <boost/json/src.hpp>
#include <iostream>
int
main ()
{
  std::cout << "create combination database at: '" + std::string { PATH_TO_DATABASE_TEST } + "'" << std::endl;
  createCombinationDatabase (PATH_TO_DATABASE_TEST);
  return 0;
}