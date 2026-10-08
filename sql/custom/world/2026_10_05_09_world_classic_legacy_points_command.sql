-- Classic 1.60 (WoW Forever): help for the GM command .legacy points (classic_legacy_commands.cpp)
DELETE FROM `command` WHERE `name` IN ('legacy', 'legacy points');
INSERT INTO `command` (`name`, `help`) VALUES
('legacy', 'Syntax: .legacy $subcommand\nType .legacy to see the list of possible subcommands or .help legacy $subcommand to see info on subcommands'),
('legacy points', 'Syntax: .legacy points [#count]\nCompletes Legacy challenge achievements for the selected player (or yourself) until #count more Legacy Points are earned, all of them without #count. Legacy Points are counted from those achievements.');
