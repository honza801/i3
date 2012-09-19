#!perl
# vim:ts=4:sw=4:expandtab
#
# Please read the following documents before working on tests:
# • http://build.i3wm.org/docs/testsuite.html
#   (or docs/testsuite)
#
# • http://build.i3wm.org/docs/lib-i3test.html
#   (alternatively: perldoc ./testcases/lib/i3test.pm)
#
# • http://build.i3wm.org/docs/ipc.html
#   (or docs/ipc)
#
# • http://onyxneon.com/books/modern_perl/modern_perl_a4.pdf
#   (unless you are already familiar with Perl)
#
# Tests the floating_{minimum,maximum}_size config options.
#
# Note that the minimum floating window size is already verified in
# t/005-floating.t.
#

use i3test i3_autostart => 0;

################################################################################
# 1: check floating_minimum_size (with non-default limits)
################################################################################

my $config = <<EOT;
# i3 config file (v4)
font -misc-fixed-medium-r-normal--13-120-75-75-C-70-iso10646-1

# Test with different dimensions than the i3 default.
floating_minimum_size 60 x 40
EOT

my $pid = launch_with_config($config);

my $window = open_floating_window(rect => [ 0, 0, 20, 20 ]);
my $rect = $window->rect;

is($rect->{width}, 60, 'width = 60');
is($rect->{height}, 40, 'height = 40');

exit_gracefully($pid);

################################################################################
# 2: check floating_minimum_size with -1 (unlimited)
################################################################################

$config = <<EOT;
# i3 config file (v4)
font -misc-fixed-medium-r-normal--13-120-75-75-C-70-iso10646-1

floating_minimum_size -1 x -1
EOT

$pid = launch_with_config($config);

cmd 'nop MEH';
$window = open_floating_window(rect => [ 0, 0, 50, 40 ]);
$rect = $window->rect;

is($rect->{width}, 50, 'width = 50');
is($rect->{height}, 40, 'height = 40');

exit_gracefully($pid);

################################################################################
# 3: check floating_maximum_size
################################################################################

$config = <<EOT;
# i3 config file (v4)
font -misc-fixed-medium-r-normal--13-120-75-75-C-70-iso10646-1

# Test with different dimensions than the i3 default.
floating_maximum_size 100 x 100
EOT

$pid = launch_with_config($config);

$window = open_floating_window(rect => [ 0, 0, 150, 150 ]);
$rect = $window->rect;

is($rect->{width}, 100, 'width = 100');
is($rect->{height}, 100, 'height = 100');

exit_gracefully($pid);

# Test that the feature works at all (without explicit configuration) by
# opening a window which is bigger than the testsuite screen (1280x1024).

$config = <<EOT;
# i3 config file (v4)
font -misc-fixed-medium-r-normal--13-120-75-75-C-70-iso10646-1
EOT

$pid = launch_with_config($config);

$window = open_floating_window(rect => [ 0, 0, 2048, 2048 ]);
$rect = $window->rect;

cmp_ok($rect->{width}, '<', 2048, 'width < 2048');
cmp_ok($rect->{height}, '<', 2048, 'height < 2048');

exit_gracefully($pid);

################################################################################
# 4: check floating_maximum_size
################################################################################

$config = <<EOT;
# i3 config file (v4)
font -misc-fixed-medium-r-normal--13-120-75-75-C-70-iso10646-1

# Test with different dimensions than the i3 default.
floating_maximum_size -1 x -1
EOT

$pid = launch_with_config($config);

$window = open_floating_window(rect => [ 0, 0, 2048, 2048 ]);
$rect = $window->rect;

is($rect->{width}, 2048, 'width = 2048');
is($rect->{height}, 2048, 'height = 2048');

exit_gracefully($pid);

done_testing;
