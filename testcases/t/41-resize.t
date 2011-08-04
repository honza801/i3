#!perl
# vim:ts=4:sw=4:expandtab
# Tests resizing tiling containers
use i3test;
use X11::XCB qw(:all);

BEGIN {
    use_ok('X11::XCB::Window');
}

my $x = X11::XCB::Connection->new;

my $tmp = fresh_workspace;

cmd 'split v';

my $top = open_standard_window($x);
sleep 0.25;
my $bottom = open_standard_window($x);
sleep 0.25;

diag("top = " . $top->id . ", bottom = " . $bottom->id);

is($x->input_focus, $bottom->id, 'Bottom window focused');

############################################################
# resize
############################################################

cmd 'resize grow up 10 px or 25 ppt';

my ($nodes, $focus) = get_ws_content($tmp);

is($nodes->[0]->{percent}, 0.25, 'top window got only 25%');
is($nodes->[1]->{percent}, 0.75, 'bottom window got 75%');


############################################################
# split and check if the 'percent' factor is still correct
############################################################

cmd 'split h';

($nodes, $focus) = get_ws_content($tmp);

is($nodes->[0]->{percent}, 0.25, 'top window got only 25%');
is($nodes->[1]->{percent}, 0.75, 'bottom window got 75%');

############################################################
# checks that resizing floating windows works
############################################################

$tmp = fresh_workspace;

$top = open_standard_window($x);
sleep 0.25;

cmd 'floating enable';

my @content = @{get_ws($tmp)->{floating_nodes}};
cmp_ok(@content, '==', 1, 'one floating node on this ws');

# up
my $oldrect = $content[0]->{rect};

cmd 'resize grow up 10 px or 25 ppt';

@content = @{get_ws($tmp)->{floating_nodes}};
cmp_ok($content[0]->{rect}->{y}, '<', $oldrect->{y}, 'y smaller than before');
cmp_ok($content[0]->{rect}->{y}, '==', $oldrect->{y} - 10, 'y exactly 10 px smaller');
cmp_ok($content[0]->{rect}->{x}, '==', $oldrect->{x}, 'x untouched');
cmp_ok($content[0]->{rect}->{height}, '>', $oldrect->{height}, 'height bigger than before');
cmp_ok($content[0]->{rect}->{height}, '==', $oldrect->{height} + 10, 'height exactly 10 px higher');
cmp_ok($content[0]->{rect}->{width}, '==', $oldrect->{width}, 'x untouched');

# up, but with a different amount of px
$oldrect = $content[0]->{rect};

cmd 'resize grow up 12 px or 25 ppt';

@content = @{get_ws($tmp)->{floating_nodes}};
cmp_ok($content[0]->{rect}->{y}, '<', $oldrect->{y}, 'y smaller than before');
cmp_ok($content[0]->{rect}->{y}, '==', $oldrect->{y} - 12, 'y exactly 10 px smaller');
cmp_ok($content[0]->{rect}->{x}, '==', $oldrect->{x}, 'x untouched');
cmp_ok($content[0]->{rect}->{height}, '>', $oldrect->{height}, 'height bigger than before');
cmp_ok($content[0]->{rect}->{height}, '==', $oldrect->{height} + 12, 'height exactly 10 px higher');
cmp_ok($content[0]->{rect}->{width}, '==', $oldrect->{width}, 'x untouched');

# left
$oldrect = $content[0]->{rect};

cmd 'resize grow left 10 px or 25 ppt';

@content = @{get_ws($tmp)->{floating_nodes}};
cmp_ok($content[0]->{rect}->{x}, '<', $oldrect->{x}, 'x smaller than before');
cmp_ok($content[0]->{rect}->{width}, '>', $oldrect->{width}, 'width bigger than before');

# right
$oldrect = $content[0]->{rect};

cmd 'resize grow right 10 px or 25 ppt';

@content = @{get_ws($tmp)->{floating_nodes}};
cmp_ok($content[0]->{rect}->{x}, '==', $oldrect->{x}, 'x the same as before');
cmp_ok($content[0]->{rect}->{y}, '==', $oldrect->{y}, 'y the same as before');
cmp_ok($content[0]->{rect}->{width}, '>', $oldrect->{width}, 'width bigger than before');
cmp_ok($content[0]->{rect}->{height}, '==', $oldrect->{height}, 'height the same as before');

# down
$oldrect = $content[0]->{rect};

cmd 'resize grow down 10 px or 25 ppt';

@content = @{get_ws($tmp)->{floating_nodes}};
cmp_ok($content[0]->{rect}->{x}, '==', $oldrect->{x}, 'x the same as before');
cmp_ok($content[0]->{rect}->{y}, '==', $oldrect->{y}, 'y the same as before');
cmp_ok($content[0]->{rect}->{height}, '>', $oldrect->{height}, 'height bigger than before');
cmp_ok($content[0]->{rect}->{width}, '==', $oldrect->{width}, 'width the same as before');

done_testing;
