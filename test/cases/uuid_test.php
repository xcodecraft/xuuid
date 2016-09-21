<?php
class UuidGeneratorTest extends PHPUnit_Framework_TestCase {

    public function test_use() {
        $mc = new Memcache();
        $mc->addServer("localhost",5001);
        $uuid = $mc->get("uuid") ;
        echo $uuid ;
        $mc->close();
        unset($mc);

    }
    public function testSDK() {
        Xuuid::conf("localhost",5001) ;
        $id = Xuuid::id();
        echo $id ;
    }

    public function test_benchmark() {
        $mc = new Memcache();
        $mc->addServer("localhost",5001);

        for($i=0;$i<=100000;$i++) {
            $uuid=$mc->get("uuid");
            if($uuid === false) {
                echo "false !!! $i\n";
                break;
            }

            if ($i % 10000 == 0) {
                $time = time();
                echo "$i, time:$time, uuid:$uuid \n";
            }
        }

        $mc->close();
        unset($mc);
    }

}
