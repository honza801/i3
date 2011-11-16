#!perl
# vim:ts=4:sw=4:expandtab
#
# Tests multiple commands (using ';') and multiple operations (using ',')
#
use i3test;

my $tmp = fresh_workspace;

sub multiple_cmds {
    my ($cmd) = @_;

    cmd 'open';
    cmd 'open';
    ok(@{get_ws_content($tmp)} == 2, 'two containers opened');

    cmd $cmd;
    ok(@{get_ws_content($tmp)} == 0, "both containers killed (cmd = $cmd)");
}
multiple_cmds('kill;kill');
multiple_cmds('kill; kill');
multiple_cmds('kill ; kill');
multiple_cmds('kill ;kill');
multiple_cmds('kill  ;kill');
multiple_cmds('kill  ;  kill');
multiple_cmds("kill;\tkill");
multiple_cmds("kill\t;kill");
multiple_cmds("kill\t;\tkill");
multiple_cmds("kill\t ;\tkill");
multiple_cmds("kill\t ;\t kill");
multiple_cmds("kill \t ; \t kill");

#####################################################################
# test if un-quoted strings are handled correctly
#####################################################################

$tmp = fresh_workspace;
cmd 'open';
my $unused = get_unused_workspace;
ok(!($unused ~~ @{get_workspace_names()}), 'workspace does not exist yet');
cmd "move workspace $unused; nop parser test";
ok(($unused ~~ @{get_workspace_names()}), 'workspace exists after moving');

#####################################################################
# quote the workspace name and use a ; (command separator) in its name
#####################################################################

cmd 'open';
$unused = get_unused_workspace;
$unused .= ';a';
ok(!($unused ~~ @{get_workspace_names()}), 'workspace does not exist yet');
cmd qq|move workspace "$unused"; nop parser test|;
ok(($unused ~~ @{get_workspace_names()}), 'workspace exists after moving');

# TODO: need a non-invasive command before implementing a test which uses ','

done_testing;
