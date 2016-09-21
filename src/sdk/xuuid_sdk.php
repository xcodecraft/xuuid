<?php

class Xuuid
{
    private static $confs = [] ;
    static public function conf($server,$port)
    {
        array_push(self::$confs,[ "server" => $server , "port" => $port] ) ;
    }
    static public function id()
    {
        static $ins  = null ;
        if ($ins == null)
        {
            $ins = new Memcache();
            foreach(self::$confs as $conf)
            {
                $ins->addServer($conf['server'],$conf['port']);
            }

        }
        return  $ins->get("uuid") ;
    }
}
