DROP TABLE register;
DROP TABLE routepaid;

DROP SEQUENCE register_registerid_seq;
DROP SEQUENCE routepaid_routepaidid_seq;
CREATE TABLE register (
	registerid bigserial,
	username varchar(20),
	password varchar(20),
	e164 varchar(30),
	credit varchar(20),
	context varchar(15)
);
CREATE TABLE routepaid (
	routepaidid bigserial,
	context varchar(15),
	prefix varchar(50),
	tehno varchar(20),
	data varchar(100),
	price INTEGER NOT NULL default '0'
);

