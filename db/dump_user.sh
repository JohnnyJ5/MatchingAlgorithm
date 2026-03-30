#!/usr/bin/bash
docker compose exec db psql -U devuser -d dating_app_db -c  'SELECT * from users'
