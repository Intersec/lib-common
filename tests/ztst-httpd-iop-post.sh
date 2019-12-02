#!/bin/sh -e

cat <<EOF | POST http://localhost:1080/iop/ -c "text/xml"
<?xml version="1.0"?>
<Envelope>
  <Body>
    <iface.fReq>
      <i>10</i>
    </iface.fReq>
  </Body>
</Envelope>
EOF
