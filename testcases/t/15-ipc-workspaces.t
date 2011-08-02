#!perl
# vim:ts=4:sw=4:expandtab

use i3test;
use List::MoreUtils qw(all);

my $i3 = i3(get_socket_path());

####################
# Request workspaces
####################

SKIP: {
    skip "IPC API not yet stabilized", 2;

my $workspaces = $i3->get_workspaces->recv;

ok(@{$workspaces} > 0, "More than zero workspaces found");

my $name_exists = all { defined($_->{name}) } @{$workspaces};
ok($name_exists, "All workspaces have a name");

}

done_testing;
