# Deferred: the sim uses the system MySQL on 127.0.0.1:3306 for now (owner decision).
# This unit is a separate MySQL 8.4 instance on 127.0.0.1:3307. Not installed by install-services.sh.
# To use it later: add /etc/forever/sim-mysql.cnf and /var/lib/forever-sim-mysql (owner approval), then point sim-db.env at port 3307 and restore the sim unit's dependency on this unit.
