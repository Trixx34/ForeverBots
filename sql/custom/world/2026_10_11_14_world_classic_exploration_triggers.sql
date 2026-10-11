-- Classic 1.60: vanilla exploration quest area triggers (VMaNGOS areatrigger_involvedrelation) missing from the VMaNGOS conversion.
-- Without them The Master's Glaive 944, In Search of the Temple 1448, 6185 and 9260-9265 could not be completed; the others add
-- the vanilla trigger next to the one we had. All trigger ids exist in the 1.60 client AreaTrigger data.
DELETE FROM `areatrigger_involvedrelation` WHERE (`id`, `quest`) IN ((3366,6025),(362,1448),(223,944),(224,944),(225,944),(97,287),(1387,3505),
(342,76),(2206,5156),(2208,5156),(2726,6185),(4092,9260),(4094,9260),(4095,9260),(4096,9260),(4098,9261),(4099,9261),(4104,9262),(4105,9262),
(4102,9263),(4101,9263),(4103,9264),(4100,9265));
INSERT INTO `areatrigger_involvedrelation` (`id`,`quest`) VALUES
(3366,6025),(362,1448),(223,944),(224,944),(225,944),(97,287),(1387,3505),(342,76),(2206,5156),(2208,5156),(2726,6185),
(4092,9260),(4094,9260),(4095,9260),(4096,9260),(4098,9261),(4099,9261),(4104,9262),(4105,9262),(4102,9263),(4101,9263),(4103,9264),(4100,9265);
