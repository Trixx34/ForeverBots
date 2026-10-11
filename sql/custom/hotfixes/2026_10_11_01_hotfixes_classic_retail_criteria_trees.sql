-- Classic 1.60: the retail (build 69497) CriteriaTree hotfixes are not Classic data (none of the 71 IDs exist in the Classic
-- CriteriaTree.db2) and 36 of them name a parent the client does not have. Pushing them makes the client fail
-- ASSERTSAFE(pParent) in AchievementInfo.cpp ("Expected parent record 138486 from record 138490", Enemy Forces).
DELETE FROM `hotfix_data` WHERE `TableHash` = 1255424668 AND `VerifiedBuild` = 69497;
DELETE FROM `criteria_tree` WHERE `VerifiedBuild` = 69497;
