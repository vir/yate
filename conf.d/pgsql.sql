DROP TABLE route;
DROP TABLE preroute;
DROP TABLE cdr;
DROP SEQUENCE route_routeid_seq;
DROP SEQUENCE preroute_prerouteid_seq;
DROP SEQUENCE cdr_cdrid_seq;
CREATE TABLE route (
	routeid bigserial,
	context varchar(15),
	prefix varchar(50),
	tehno varchar(20),
	data varchar(100)
);

CREATE TABLE preroute (
	prerouteid bigserial,
	tehno varchar(20),
	channel varchar(20),
	caller varchar(30),
	called varchar(30),
	context varchar(15)
);

CREATE TABLE cdr (
	cdrid bigserial,
	time TIMESTAMP with time zone NOT NULL DEFAULT now(),
	channel varchar(20),
	caller varchar(30),
	called varchar(30),
	billtime INTEGER NOT NULL default '0',
	ringtime INTEGER NOT NULL default '0',
	duration INTEGER NOT NULL default '0',
	status varchar(15)
);

INSERT INTO route (context,prefix,tehno,data) VALUES ('paul','1','SIP','jen');
INSERT INTO route (context,prefix,tehno,data) VALUES ('bell','112','SIP','bell');
INSERT INTO route (context,prefix,tehno,data) VALUES ('gigi','4021','ZAP','1');
INSERT INTO route (context,prefix,tehno,data) VALUES ('whatever','40238','ZAP','g1');
INSERT INTO route (context,prefix,tehno,data) VALUES ('default','1','SIP','jen');
INSERT INTO route (context,prefix,tehno,data) VALUES ('default','2','SIP','bell');
INSERT INTO route (context,prefix,tehno,data) VALUES ('default','3','ZAP','g1');
INSERT INTO route (context,prefix,tehno,data) VALUES ('default','4','ZAP','g2');
INSERT INTO route (context,prefix,tehno,data) VALUES ('default','11','ZAP','g11');
INSERT INTO route (context,prefix,tehno,data) VALUES ('default','12','ZAP','g12');
INSERT INTO route (context,prefix,tehno,data) VALUES ('default','13','ZAP','g13');
INSERT INTO route (context,prefix,tehno,data) VALUES ('default','14','ZAP','g14');
INSERT INTO route (context,prefix,tehno,data) VALUES ('default','15','ZAP','g15');
INSERT INTO route (context,prefix,tehno,data) VALUES ('default','16','ZAP','g16');
INSERT INTO route (context,prefix,tehno,data) VALUES ('default','17','ZAP','g17');
INSERT INTO route (context,prefix,tehno,data) VALUES ('default','18','ZAP','g18');
INSERT INTO route (context,prefix,tehno,data) VALUES ('default','19','ZAP','g19');



INSERT INTO preroute (tehno,channel,caller,called,context) VALUES ('Zap','1','5556','','default');
INSERT INTO preroute (tehno,channel,caller,called,context) VALUES ('Zap','1','1','','default');
INSERT INTO preroute (tehno,channel,caller,called,context) VALUES ('Zap','1','2','','bell');
INSERT INTO preroute (tehno,channel,caller,called,context) VALUES ('Zap','1','12','','gigi');



INSERT INTO cdr (channel,caller,called,billtime,ringtime,duration) VALUES ('Zap','1','12','34','10','45');

