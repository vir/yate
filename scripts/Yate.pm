#######################################################
#
# YateMessage.pm gateway interface module for Yate
# Copyright Anthony Minessale II <anthmct@yahoo.com> 
# This Module is released under the GNU Public License
# If you find this software useful, donations are welcome
# at paypal:jillkm3@yahoo.com
#
package Yate;
use Data::Dumper;
use POSIX;

#disable Buffering
$|=1;

sub new($;$$) {
  my $proto = shift;
  my $class = ref($proto) || $proto;
  my $name = shift;
  my $params = shift;
  my $id = (int rand 1000) . time;
  my $self = {params => $params , headers => { name => $name , what => "message", id => $id, processed => "false" , time => time} };

  bless ($self, $class);

}

sub warn($;) {
  my($self) = shift;
  printf STDERR @_;
}

sub dump($) {
  my $self = shift;
  $self->warn(Dumper $self);
}

sub print($$) {
  my($self,$buf) = @_;
  # replace any % character with %%
  $buf =~ s/\%/\%\%/g;
  # translate any char with an ascii code < 32 into the escaped form
  $buf =~ s/(.)/ord $1 < 32 ? sprintf "%%%s", chr(ord($1) + 64) : sprintf "%s",$1/eg;
  $self->warn("DEBUG:OUT: $buf") if($ENV{DEBUGYATE});
  print STDOUT $buf;
}


sub handle_install($$) {
  my $self = shift;
  my ($what,$pri,$driver,$tf) = @_;
  if($what eq "install") {
    if($tf eq "false") {
      $self->warn("Error installing $driver at priority $pri\n");
      delete $self->{funcs}->{$driver};
    }
  }
  elsif($what eq "uninstall") {
    if($tf eq "true") {
      $self->warn("Un-installing $driver at priority $pri\n");
      delete $self->{funcs}->{$driver};
    }
  }

}
sub install($$$;$) {
  my ($self,$driver,$func,$pri) = @_;
  $pri ||= 10;
  $self->print("%>install:$pri:$driver\n");
  $self->warn("install $driver priority $pri\n");
  push @{$self->{funcs}->{$driver}},$func if($func);
}

sub uninstall($$) {
  my ($self,$driver) = @_;
  $self->print("%>uninstall:$driver\n");
}

sub param($$;$) {
  my($self,$param,$val) = @_;
  if($param and defined $val) {
    $self->{params}->{$param} = $val;
  }
  $self->{params}->{$param};
}

sub header($$;$) {
  my($self,$header,$val) = @_;
  if($header and $val) {
    $self->{headers}->{$header} = $val;
  }
  $self->{headers}->{$header};
}

sub params($;$) {
  my($self,$params) = @_;
  if($params) {
    $self->{params} = $params;
  }
  $self->{params}
}

sub headers($;$) {
  my($self,$headers) = @_;
  if($headers) {
    $self->{headers} = $headers;
  }
  $self->{headers}
}

sub name($;$) {
  my ($self,$set) = @_;
  $self->{headers}->{name} = $set if(defined $set);
  $self->{headers}->{name};
}

sub what($;$) {
  my ($self,$set) = @_;
  $self->{headers}->{what} = $set if(defined $set);
  $self->{headers}->{what};
}

sub id($;$) {
  my ($self,$set) = @_;
  $self->{headers}->{id} = $set if(defined $set);
  $self->{headers}->{id};
}

sub time($;$) {
  my $self = shift;
  my $fmt = shift;
  if($fmt =~ /^\d+$/) {
    $self->{headers}->time = $fmt;
    $fmt = undef;
  }
  if($fmt) {
    return strftime($fmt,localtime($self->{headers}->{time}));
  }
  $self->{headers}->time;
}

sub retvalue($;$) {
  my ($self,$set) = @_;
  $self->{headers}->{retvalue} = $set if(defined $set);
  $self->{headers}->{retvalue};
}

sub processed($;$) {
  my ($self,$set) = @_;
  if($set =~ /^true$|^false$/i) {
    $self->{headers}->{processed} = $set;
    $self->{headers}->{processed} = $set ? "true" : "false" if(defined $set);
  }
  $self->{headers}->{processed};
}

sub parse($;$) {
  my($self,$rawstring) = @_;
  my ($params,$headers);
  $self->warn("DEBUG:IN: $rawstring") if($ENV{DEBUGYATE});
  $self->{_rawstring} ||= $rawstring;
  chomp $self->{_rawstring};
  my @array = split(":",$self->{_rawstring});
  my $header = shift @array;
  delete $self->{headers};
  delete $self->{params};

  # decode any escaped ascii chars
  $header =~ s/\%\%/\%/g;
  ($headers->{direction},$headers->{what}) = $header =~ /^\%(.)([^:]+)/;
  if($headers->{what} =~ /install/) {
    $self->handle_install($headers->{what},@array);
    return 1;
  }
  s/\%([^\%])/chr ((ord $1) - 64)/eg for(@array);
  $headers->{id} = shift @array;
  $headers->{time} = shift @array;
  $headers->{name} = shift @array;
  $headers->{retvalue} = shift @array;
  foreach(@array) {
    my($var,$val) = split("=",$_,2);
    $params->{$var} = $val;
  }

  $self->headers($headers);
  $self->params($params);
  return 1;
}

sub error($;) {
  my ($self) = shift;
  my $fmt = shift;
  $fmt .= "\n" if($fmt !~ /\n$/);
  $self->warn("Error: {\n$self->{_rawstring}\n$fmt}\n",$self->what,strftime("%d %t",localtime),@_);
}


sub dispatch($$) {
  my ($self) = shift;
  $self->processed(shift);
  my @params;
  my $paramstr = undef;
  foreach(keys %{$self->{params}}) {
    push @params,"$_=$self->{params}->{$_}";
  }
  $paramstr = join(":",@params) if(@params);
  $self->print(sprintf "%%<%s:%s:%s:%s:%s\n",
	       $self->{headers}->{what},
	       $self->{headers}->{id},
	       $self->{headers}->{processed},
	       $self->{headers}->{name},
	       $self->{headers}->{retvalue},
	       $paramstr);

}

sub exec($;$) {
  my $self = shift;
  my $ok = shift || 0;
  my $exec = 0;
  foreach my $func (@{$self->{funcs}->{$self->{headers}->{name}}}) {
    my $ret = $func->($self);
    if($ret) {
      $self->dispatch($ret);
      $ok++;
      $exec++;
      last;
    }
  }
  if(!$ok and $self->what eq "message") {
    $self->error("Nobody accepted the request.  Returning the message.\n");
    $self->dispatch("false");
  }
  $exec;
}


sub raw_read($) {
  my $self = shift;
  $self->{_rawstring} = <STDIN>;
  return $self->{_rawstring};
}

sub read_data($) {
  my $self = shift;
  if($self->raw_read()) {
    return $self->parse();
  }
  return 0;
}

sub listen($) {
  my $self = shift;
  while($self->read_data()) {
    $self->exec();
  }
}

1;
