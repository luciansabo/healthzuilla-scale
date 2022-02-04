openssl req -x509 -nodes -days 3650 -newkey rsa:2048 -keyout key.pem -out cert.pem -config openssl.cnf

echo 'static const char serverCert[] PROGMEM = R"EOF(' > ssl-config.h
cat cert.pem >> ssl-config.h
echo ')EOF";'  >> ssl-config.h
echo 'static const char serverKey[] PROGMEM =  R"EOF(' >> ssl-config.h
cat key.pem >> ssl-config.h
echo ')EOF";' >> ssl-config.h
echo 'Generated ssl-config.h';
