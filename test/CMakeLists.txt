add_library(seatest seatest.c)
install (TARGETS seatest DESTINATION test_bin)
install (FILES seatest.h DESTINATION test_include)