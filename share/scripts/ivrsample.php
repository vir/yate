#!/usr/bin/php -q
<?
/* Simple example for the object oriented IVR interface

   To use it you must route a number to:
   external/nodata/ivrsample.php
*/
require_once("libyateivr.php");

class IVR1 extends IVR
{

    function OnEnter($state)
    {
	$this->optable = array(
	    ":5" => "call ivr2:b",
	    ":6" => "jump ivr2:b",
	    ":8" => "return",
	);
	parent::OnEnter($state);
	IVR::Call("ivr2");
    }

    function OnDTMF($tone)
    {
	$this->Output("Got $tone");
	if (parent::OnDTMF($tone))
	    return true;
	if ($tone == "#")
	    IVR::Call("ivr2");
	return true;
    }
}

class TheIVR_2 extends IVR
{
    function OnEnter($state)
    {
	parent::OnEnter($state);
	IVR::Call("nosuch");
    }

    function OnDTMF($tone)
    {
	switch ($tone) {
	    case "*":
		IVR::Hangup();
		break;
	    case "#":
		IVR::Leave("Got $tone");
		break;
	    case "0":
		$this->Output("Got $tone");
		break;
	    default:
		return false;
	}
	return true;
    }
}

Yate::Init();
Yate::Debug(true);

IVR::Register("ivr1","IVR1");
IVR::Register("ivr2","TheIVR_2");

IVR::Run("ivr1");

Yate::Output("PHP: bye!");

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
