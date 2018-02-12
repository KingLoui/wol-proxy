# wol-proxy

##Intention
Most SoHo routers do not allow to forward magic packets for wake-on-lan to your internal broadcast address. As a result you cannot use wake-on-lan over the internet (wake-on-wan) to wake your equipment at home. Those routers allow only a forward to a fixed internal ip address which does not work with wake-on-lan. More details can you read here: wake-on-wan.
A Raspberry Pi is an energy efficient, low cost computer which we will use to create a wake-on-lan proxy. Your router will send the magic packet to the Pi and the Pi will then forward the magic packet to the local subnet broadcast address.

