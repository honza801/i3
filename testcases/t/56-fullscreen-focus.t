#!perl
# vim:ts=4:sw=4:expandtab
#
# Test if new containers get focused when there is a fullscreen container at
# the time of launching the new one.
#
use X11::XCB qw(:all);
use i3test;

BEGIN {
    use_ok('X11::XCB::Window');
}

my $x = X11::XCB::Connection->new;
my $i3 = i3(get_socket_path());

my $tmp = fresh_workspace;

#####################################################################
# open the left window
#####################################################################

my $left = open_standard_window($x, '#ff0000');

is($x->input_focus, $left->id, 'left window focused');

diag("left = " . $left->id);

#####################################################################
# Open the right window
#####################################################################

my $right = open_standard_window($x, '#00ff00');

diag("right = " . $right->id);

#####################################################################
# Set the right window to fullscreen
#####################################################################
cmd 'nop setting fullscreen';
cmd 'fullscreen';

#####################################################################
# Open a third window
#####################################################################

my $third = open_standard_window($x, '#0000ff');

diag("third = " . $third->id);

# move the fullscreen window to a different ws

my $tmp2 = get_unused_workspace;

cmd "move workspace $tmp2";

# verify that the third window has the focus

sleep 0.25;

is($x->input_focus, $third->id, 'third window focused');

done_testing;
