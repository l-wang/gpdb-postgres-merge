-- Test CLUSTER with append optimized storage
CREATE TABLE cluster_ao_table(
	id int,
	fname text,
	lname text,
	address1 text,
	address2 text,
	city text,
	state text,
	zip text)
WITH (appendonly=true)
DISTRIBUTED BY (id);

INSERT INTO cluster_ao_table (id, fname, lname, address1, address2, city, state, zip)
SELECT i, 'Jon_' || i, 'Roberts_' || i, i || ' Main Street', 'Apartment ' || i, 'New York', 'NY', i::text
FROM generate_series(1, 10000) AS i;

CREATE INDEX ON cluster_ao_table (id);

CLUSTER cluster_ao_table USING cluster_ao_table_id_idx;

SELECT * FROM cluster_ao_table WHERE id = 10;

DROP TABLE cluster_ao_table;

-- Test CLUSTER with append optimized columnar storage
CREATE TABLE cluster_ao_table(
	id int,
	fname text,
	lname text,
	address1 text,
	address2 text,
	city text,
	state text,
	zip text)
WITH (appendonly=true, orientation=column)
DISTRIBUTED BY (id);

INSERT INTO cluster_ao_table (id, fname, lname, address1, address2, city, state, zip)
SELECT i, 'Jon_' || i, 'Roberts_' || i, i || ' Main Street', 'Apartment ' || i, 'New York', 'NY', i::text
FROM generate_series(1, 10000) AS i;

CREATE INDEX ON cluster_ao_table (id);

CLUSTER cluster_ao_table USING cluster_ao_table_id_idx;

SELECT * FROM cluster_ao_table WHERE id = 10;

DROP TABLE cluster_ao_table;
