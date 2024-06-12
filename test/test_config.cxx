//
// Created by walde on 2/11/24.
//
#include "modern_durak_game/util/util.hxx"
#include "test/constant.hxx"
#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>
#include <filesystem>
#include <iostream>

int
main (int argc, char *argv[])
{
  if (std::filesystem::exists (DEFAULT_DATABASE_PATH))
    {
      return Catch::Session ().run (argc, argv);
    }
  else
    {
      std::cerr << "combination.db not found at: '" + DEFAULT_DATABASE_PATH + "' please create it by running create_combination_database executable. Consider building create_combination_database in release mode it is around 15 times faster than debug." << std::endl;
      return 1;
    }
}