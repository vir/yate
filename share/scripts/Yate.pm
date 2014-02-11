#
# Yate.pm
#
# This file is part of the YATE Project http://YATE.null.ro
#
# Gateway interface module for Yate.
#
# Yet Another Telephony Engine - a fully featured software PBX and IVR
# Copyright (C) 2004-2006 Null Team
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
#

package Yate;

use strict;
use warnings;

# Executed before everything.
BEGIN {
    # Have at least perl 5.6.1 to use this module.
    use 5.006_001;
    use Data::Dumper;

    # Set version && disable output buffering.
    our $VERSION = '0.22';
    $ |= 1;
}

# All messages syntax.
my %headers = (
    # keyword         # fields
    'Error in'     => ['keyword', 'original'],
    '%%>message'   => ['keyword', 'id', 'time', 'name', 'retvalue'],
    '%%<message'   => ['keyword', 'id', 'processed', 'name', 'retvalue'],
    '%%<install'   => ['keyword', 'priority', 'name', 'success'],
    '%%<uninstall' => ['keyword', 'priority', 'name', 'success'],
    '%%<watch'     => ['keyword', 'name', 'success'],
    '%%<unwatch'   => ['keyword', 'name', 'success'],
    '%%<setlocal'  => ['keyword', 'name', 'value', 'success'],
);

# Act as OOP module.
sub new($;@) {
    my $class = shift;
    $class = ref($class) || $class;

    # Accept only 'Debug' as additional parameter.
    my $self = {
	'Debug' => 0,
	@_,
    };

    bless($self, $class);

    # Install internal handlers.
    $self->install_handlers();

    return $self;
}

# Install default handlers.
sub install_handlers($) {
    my ($self) = @_;

    push(@{$self->{'_handlers'}->{'install'}}, \&handle_install);
    push(@{$self->{'_handlers'}->{'uninstall'}}, \&handle_uninstall);
    push(@{$self->{'_handlers'}->{'watch'}}, \&handle_watch);
    push(@{$self->{'_handlers'}->{'unwatch'}}, \&handle_unwatch);
    push(@{$self->{'_handlers'}->{'setlocal'}}, \&handle_setlocal);
    push(@{$self->{'_handlers'}->{'Error in'}}, \&handle_error_in);
}

# Default handler for install.
sub handle_install($) {
    my ($self) = @_;

    if ($self->header('success') eq 'true') {
	$self->debug('Installed handler for ' . $self->header('name') . ' at priority ' . $self->header('priority') . '.') if ($self->{'Debug'} == 1);
    } else {
	$self->error('Cannot install handler for ' . $self->header('name') . '.');

	delete($self->{'_handlers'}->{$self->header('name')}) if (exists($self->{'_handlers'}->{$self->header('name')}));
    }
}

# Default handler for uninstall.
sub handle_uninstall($) {
    my ($self) = @_;

    if ($self->header('success') eq 'true') {
	$self->debug('Uninstalled handler for ' . $self->header('name') . ' at priority ' . $self->header('priority') . '.') if ($self->{'Debug'} == 1);
    } else {
	$self->error('Cannot uninstall handler for ' . $self->header('name') . '.');
    }
}

# Default handler for watch.
sub handle_watch($) {
    my ($self) = @_;

    if ($self->header('success') eq 'true') {
	$self->debug('Installed watcher for ' . $self->header('name') . '.') if ($self->{'Debug'} == 1);
    } else {
	$self->error('Cannot install watcher for ' . $self->header('name') . '.');

	delete($self->{'_watchers'}->{$self->header('name')}) if (exists($self->{'_watchers'}->{$self->header('name')}));
    }
}

# Default handler for unwatch.
sub handle_unwatch($) {
    my ($self) = @_;

    if ($self->header('success') eq 'true') {
	$self->debug('Uninstalled watcher for ' . $self->header('name') . '.') if ($self->{'Debug'} == 1);
    } else {
	$self->error('Cannot uninstall watcher for ' . $self->header('name') . '.');
    }
}

# Default handler for setlocal.
sub handle_setlocal($) {
    my ($self) = @_;

    if ($self->header('success') eq 'true') {
	$self->debug('Changed local parameter ' . $self->header('name') . ' to ' . $self->header('value') . '.') if ($self->{'Debug'} == 1);
    } else {
	$self->error('Cannot change local parameter ' . $self->header('name') . ' to ' . $self->header('value') . '.');
    }
}

# Default handler for 'Error in:' Engine to Application message.
sub handle_error_in($) {
    my ($self) = @_;

    $self->error('Received error in message we sent: ' . $self->header('original') . '.');
}

# Common install subroutine.
sub _install($$$$) {
    my ($self, $name, $type, $handler) = @_;

    # Sanity checks for name, type and handler.
    if (!$name || !$type) {
	$self->error('Called install for event with no name or type.');

	return 0;
    }

    if (ref($handler) ne 'CODE') {
	$self->error('Handler for event is not a code reference (install).');

	return 0;
    }

    # Check if this is the first entry. If it is return 1.
    my $return = not exists($self->{$type}->{$name});

    # Keep track of all handlers for an event Insert handler in array.
    push(@{$self->{$type}->{$name}}, $handler);

    return $return;
}

# Install a handler for an event (message event).
sub install($$$;$;$$) {
    my ($self, $name, $handler, $priority, $filter_name, $filter_value) = @_;

    # If this is the first entry send a notice to the Engine.
    if ($self->_install($name, '_handlers', $handler) == 1) {
	# Default priority is 100.
	$priority = 100 if (!$priority);

	# Format is %%>install:prior:name[:filter-name:[filter-value]].
	my $query = sprintf('%%%%>install:%s:%s', $self->escape($priority, ':'), $self->escape($name, ':'));

	# filter-name and filter-value are optional.
	if ($filter_name) {
	    $filter_value = '' unless defined $filter_value;
	    $query .= sprintf(':%s:%s', $self->escape($filter_name, ':'), $self->escape($filter_value, ':'));
	}

	$self->print($query);
    }
}

# Install a handler for an internal event (like 'Error in').
sub install_internal($$$;$;$$) {
    my $self = shift;

    $self->install(@_);
}

# Install a handler for an event (and exactly incoming message).
sub install_incoming($$$) {
    my ($self, $name, $handler) = @_;

    $self->_install($name, '_incoming_handlers', $handler);
}

# Install a watcher.
sub install_watcher($$$) {
    my ($self, $name, $handler) = @_;

    if ($self->_install($name, '_watchers', $handler) == 1) {
	# Format is %%>watch:name.
	$self->print(sprintf('%%%%>watch:%s', $self->escape($name, ':')));
    }
}

# Common uninstall subroutine.
sub _uninstall($$$;$) {
    my ($self, $name, $type, $handler) = @_;

    # Check if event exists at all.
    if (not exists($self->{$type}->{$name})) {
    	return 0;
    }

    # Handler is given - delete only one.
    if ($handler) {
	# Just in case.
	if (ref($handler) ne 'CODE') {
	    $self->error('Handler for event is not a code reference.');

	    return 0;
	}

	# Because we keep handlers in array we must loop the array, find
	# the handler position and delete it.
	for (my $i = 0; $i <= $#{$self->{$type}->{$name}}; $i++) {
	    if (${$self->{$type}->{$name}}[$i] == $handler) {
		splice(@{$self->{$type}->{$name}}, $i, 1);

		last;
	    }
	}
    }

    # Time for deleting this event?
    if (!$handler || $#{$self->{$type}->{$name}} == -1) {
	delete($self->{$type}->{$name});

	return 1;
    }

    return 0;
}

# Uninstall a handler or all handlers for an event (message event).
sub uninstall($$;$) {
    my ($self, $name, $handler) = @_;

    if ($self->_uninstall($name, '_handlers', $handler) == 1) {
	# Format is %%>uninstall:name.
	$self->print(sprintf('%%%%>uninstall:%s', $self->escape($name, ':')));
    }
}

# Uninstall an internal handler or all internal handlers for an event
# (internal event like 'Error in').
sub uninstall_internal($$;$) {
    my $self = shift;

    $self->uninstall(@_);
}

# Uninstall an incoming message handler or all incoming message handlers
# for an event.
sub uninstall_incoming($$;$) {
    my ($self, $name, $handler) = @_;

    $self->_uninstall($name, '_incoming_handlers', $handler);
}

# Uninstall a watcher or all watcher for an event.
sub uninstall_watcher($$;$) {
    my ($self, $name, $handler) = @_;

    if ($self->_uninstall($name, '_watchers', $handler) == 1) {
	# Format is %%>unwatch:name.
	$self->print(sprintf('%%%%>unwatch:%s', $self->escape($name, ':')));
    }
}

# Wait for messages on STDIN (default behaviour).
sub listen($) {
    my ($self) = @_;

    while (my $line = <STDIN>) {
	# Get rid of \n at the end.
	chomp($line);

	# Message received. Parse && dispatch (if parse succeeds).
	if ($self->parse_message($line) == 1) {
	    $self->dispatch();
	}
    }
}

# Parses messages and splits it to parts.
sub parse_message($$) {
    my ($self, $line) = @_;

    # Empty line?
    if (!$line) {
	return 0;
    }

    # That looks like a happy face :)
    my ($msg_headers, $msg_params) = ({}, {});

    $self->debug('Got message: ' . $line . '.') if ($self->{'Debug'} == 1);

    if ($line =~ /^(.+?):/ && exists($headers{$1})) {
	my $header = $1;

	# Removes the prefix and puts it in header.
	if ($line !~ s/^(%%[><])//) {
	    $msg_headers->{'prefix'} = '';
	} else {
	    $msg_headers->{'prefix'} = $1;
	}

	# Split line according to %headers (unescape fields).
	my @split_line = split(/:/, $line, $#{$headers{$header}} + 2);
	if ($#split_line < $#{$headers{$header}}) {
	    $self->error('Got invalid count of parameters for keyword ' . $header . '.');

	    return 0;
	}

	for (my $i = 0; $i < @{$headers{$header}}; $i++) {
	    $msg_headers->{${$headers{$header}}[$i]} = $self->unescape($split_line[$i]);
	}

	# Now handle params (if any).
	if ($#split_line > $#{$headers{$header}}) {
	    foreach (split(/:/, $split_line[$#split_line])) {
		my ($key, $value) = split(/=/, $_, 2);

		if ($key) {
		    $msg_params->{$self->unescape($key)} = $self->unescape($value);
		}
	    }
	}
    } else {
	$self->error('Got invalid keyword header.');

	return 0;
    }

    # Set headers and params in main object.
    $self->headers($msg_headers);
    $self->params($msg_params);

    return 1;
}

# Opposite to escape(). Convert %% to % and %'upper_code' to ASCII.
sub unescape($$) {
    my ($self, $string) = @_;

    if ($string) {
	$string =~ s/%(.)/$1 eq '%' ? '%' : (ord($1) < 64 ? $self->error('Invalid character sequence passed to unscape.') : chr(ord($1) - 64))/egs;
    }

    return $string;
}

# Convert % to %%, and every ASCII char with code < 32 to %'upper_code' as stated in yate.null.ro/docs/extmodule.html.
sub escape($$;$) {
    my ($self, $string, $special) = @_;

    if ($string) {
	$string =~ s/(.)/$1 eq '%' ? '%%' : (ord($1) < 32 || ($special && $1 eq $special) ? '%' . chr(ord($1) + 64) : $1)/egs;
    }

    return $string;
}

# Set/Return all params.
sub params($;$) {
    my ($self, $params) = @_;

    if ($params) {
	$self->{'params'} = $params;
    } elsif (exists($self->{'params'})) {
	return $self->{'params'};
    }

    return undef;
}

# Set/Return all headers.
sub headers($;$) {
    my ($self, $headers) = @_;

    if ($headers) {
	$self->{'headers'} = $headers;
    } elsif (exists($self->{'headers'})) {
	return $self->{'headers'};
    }

    return undef;
}

# Set/Return a param.
sub param($$;$) {
    my ($self, $key, $value) = @_;

    if (!$key) {
	return undef;
    }

    if (defined $value) {
	$self->{'params'}->{$key} = $value;
    } elsif (exists($self->{'params'}->{$key})) {
	return $self->{'params'}->{$key};
    }

    return undef;
}

# Set/Return a header.
sub header($$;$) {
    my ($self, $key, $value) = @_;

    if (!$key) {
	return undef;
    }

    if ($value) {
	$self->{'headers'}->{$key} = $value;
    } elsif (exists($self->{'headers'}->{$key})) {
	return $self->{'headers'}->{$key};
    }

    return undef;
}

# Tries to call a handler for the given keyword.
sub dispatch($) {
    my ($self) = @_;

    # It's not a message, check for 'keyword' binded handlers.
    if ($self->header('keyword') ne 'message') {
	if (exists($self->{'_handlers'}->{$self->header('keyword')})) {
	    foreach (@{$self->{'_handlers'}->{$self->header('keyword')}}) {
		$_->($self);
	    }
	}

	return 1;
    }

    # It is a message. We assume we have 'name' and 'prefix'.

    if ($self->header('prefix') eq '%%<') {
	my $type;

	# If there's no ID, it is for the watcher.
	if ($self->header('id')) {
	    $type = '_incoming_handlers';
	} else {
	    $type = '_watchers';
	}

	if (exists($self->{$type}->{$self->header('name')})) {
	    foreach (@{$self->{$type}->{$self->header('name')}}) {
		$_->($self);
	    }
	}

	return 1;
    }

    if (not exists($self->{'_handlers'}->{$self->header('name')})) {
	$self->error('No handler for event ' . $self->header('name') . '.') if ($self->{'Debug'} == 1);

	return 0;
    }

    foreach (@{$self->{'_handlers'}->{$self->header('name')}}) {
	my $return = $_->($self);

	if (defined($return) && lc($return) ne 'false' && $return ne '0') {
	    $self->return_message('true', $return);

	    return 1;
	}
    }

    $self->return_message('false', '');
    $self->error('Could not dispatch event ' . $self->header('name') . '.') if ($self->{'Debug'} == 1);

    return 0;
}

# Send a response message to Engine.
sub return_message($$$) {
    my ($self, $processed, $return_value) = @_;

    # Not likely to happen.
    if ($self->header('keyword') ne 'message' || $self->header('prefix') ne '%%>') {
	$self->error('return_message() called, but no %%>message received before that!');

	return 0;
    }

    my $params = '';
    if (ref($self->params()) eq 'HASH') {
	while (my ($key, $value) = each(%{$self->params()})) {
	    if ($key) {
		$value = '' unless defined $value;
		$params .= ':' . $self->escape($key, ':') . '=' . $self->escape($value, ':');
	    }

	}
    }

    # Format is %%<message:id:processed:name:return_value[:key=value:...].
    $self->print(sprintf('%%%%<message:%s:%s:%s:%s%s',
	$self->escape($self->header('id'), ':'),
	$self->escape($processed, ':'),
	$self->escape($self->header('name'), ':'),
	$self->escape($return_value, ':'),
	$params));

    return 1;
}

# Output a message to STDERR.
sub output_error($$) {
    my ($self, $message) = @_;

    if ($message) {
	print STDERR $message . "\n";
    }
}

# Output a DEBUG message.
sub debug($$) {
    my ($self, $message) = @_;

    if ($message) {
	$self->output_error('DEBUG: ' . $message);
    }
}

# Output an ERROR message.
sub error($$) {
    my ($self, $message) = @_;

    if ($message) {
	$self->output_error('ERROR: ' . $message);
    }
}

# Print message to Engine.
sub print($$) {
    my ($self, $message) = @_;

    if ($message) {
	$self->debug('Printing ' . $message) if ($self->{'Debug'} == 1);

	print STDOUT $message . "\n";
    }
}

# Send a message to the Engine (like chan.masquarade).
sub message($$$;$;@) {
    my ($self, $name, $return_value, $id, %params) = @_;

    # Input checks for name, return_value and id.
    if (!$name) {
	return 0;
    }

    $return_value = '' unless defined $return_value;

    if (!$id) {
	$id = generate_id();
    }

    my $params = '';
    while (my ($key, $value) = each(%params)) {
	if ($key) {
	    $value = '' unless defined $value;
	    $params .= ':' . $self->escape($key, ':') . '=' . $self->escape($value, ':');
	}
    }

    # %%>message:id:time:name:return_value[:key=value:...]
    $self->print(sprintf('%%%%>message:%s:%s:%s:%s%s',
	$self->escape($id, ':'),
	time(),
	$self->escape($name, ':'),
	$self->escape($return_value, ':'),
	$params));

    return 1;
}

# Generate a random sequence of characters (10). Used for unique IDs.
sub generate_id($) {
    my ($self) = @_;

    my @chars = ('A'..'Z', 'a'..'z', 0..9);
    return join('', @chars[map{rand @chars}(1..10)]);
}

# Send a message to the Engine for logging.
sub output($$) {
    my ($self, $message) = @_;

    if ($message) {
	# Format is %%>output:message.
	# Simple, no escaping, no nothing.
	$self->print('%%>output:' . $message);
    }
}

# Send a setlocal message to the Engine (request local parameter change).
sub setlocal($$$) {
    my ($self, $name, $value) = @_;

    if (!$name || !defined($value)) {
	$self->error('Invalid arguments given to setlocal().');

	return 0;
    }

    if ($name eq 'timeout') {
	if ($value = '' || $value =~ /[^0-9]/) {
	    $self->error('Called setlocal with invalid parameters (value is not a number).');

	    return 0;
	}
    } elsif ($name eq 'disconnected' || $name eq 'timebomb' || $name eq 'reenter' || $name eq 'selfwatch') {
	if ($value ne 'true' && $value ne 'false') {
	    $self->error('Called setlocal with invalid parameters (value is not a boolean (true/false)).');

	    return 0;
	}
    } elsif ($name ne 'id') {
	$self->error('Called setlocal with invalid name (' . $name . ').');

	return 0;
    }

    # Format is %%>setlocal:name:value.
    $self->print(sprintf('%%%%>setlocal:%s:%s',
	$self->escape($name, ':'),
	$self->escape($value, ':')));

    return 1;
}

# Dump itself to STDERR.
sub dump($) {
    my ($self) = @_;

    $self->debug(Dumper($self));
}

# Old subroutines. We keep it for backwards compatibillity.
sub retval($$) {
    my $self = shift;

    $self->error('Using retval or retvalue is depricated. Please use a simple return for returning a value.');
}

sub retvalue($$) {
    my $self = shift;

    $self->error('Using retval or retvalue is depricated. Please use a simple return for returning a value.');
}

1;

# vi: set ts=8 sw=4 sts=4 noet: #

__END__

=head1 NAME

Yate - Gateway interface module for YATE (Yet Another Telephone Engine)

=head1 SYNOPSIS

    use strict;
    use warnings;
    use Yate;

    sub call_route_handler($) {
        my $message = shift;

        # Event is processed and returns the following:
        return 'sip/sip:' . $message->param('called') . '@11.11.11.11';
    }

    sub call_execute_handler($) {
        my $message = shift;

        $message->dump();

        # Event is not processed. You can use 0 or undef instead.
        # or just don't use return.
        return 'false';
    }

    my $message = new Yate();
    # call.route, call.execute or any other event.
    $message->install('call.route', \&call_route_handler);
    # This processes events from other modules like conference.cpp.
    $message->install_watcher('call.execute', \&call_execute_handler);
    $message->listen();

=head1 DESCRIPTION

This module provides interface for using Perl scripts as plugins in
Yate. With this developers can easily write a script that interacts
with Yate Engine. The scripts can track certain events (like
call.cdr), and return values which will change the state of the call
(i.e. with call.route you can set the route of the call).

=head1 METHODS

=head2 new

    my $message = new Yate(Debug => 1)

Creates an Yate object. Attributes are:

    Debug		Verbose debugging.

By creating such an object you automatically create default handlers
for "install", "uninstall", "watch", "unwatch", "setlocal" and
"Error in" messages. Those are handled by the module itself (thou you
can overwrite them).

=head2 install, install_incoming, install_internal, install_watcher

    $message->install($event, \&handler, $priority, filter_name, $filter_value)
    $message->install_internal($event, \&handler, $priority, filter_name, $filter_value)
    $message->install_incoming($event, \&handler)
    $message->install_watcher($event, \&handler)

Attaches a handler to an event. All events can be found at
http://yate.null.ro/pmwiki/index.php?n=Main.StandardMessages. In
addition to those you can use install, uninstall and setlocal with
C<install_internal>. Those are internal messages sent between Engine
and Application, so it is recommended not to use them. The difference
between C<install> and C<install_incoming> is that install_incoming
handles answers to your C<message>. For installing a "watcher" use
C<install_watcher>. Watcher triggers events owned by other modules
like conference.cpp or another perl script.

The priority is optional and defaults to 100. It can be any value and
it forces Yate to either first process another module (core module
like regexroute for example) or process first this script and then the
other modules (if any). Priority is taken only for the first install
call for each different event.

Filter name and filter value are optional. Filter name is the
name of the variable the handler will filter and filter value
is its value.

When an event accures your subroutine(s) will be called. If one of
your subroutines returns something different then "false", 0 and
undef, no other subroutine will be called. This applies to all events.

If your events returns "false", 0, undef or it does not return anything
at all, message is considered as not processed. If you want to return
a value to the Enigne, just use return "some_string" (applies only for
handlers installed with C<install>). A message will be send to the
Engine with all the parameters and of course the return value. If you
wish to process a message, but not to send anything back to the Engine
return ''.

Handlers will have only one argument and that is the current Yate
object itself. You can access headers and parameters by C<header> and
C<param>. For debugging the current structure of the object use C<dump>.

The program starts listening for events after you call C<listen>.

You might sometimes need to send a message to the Yate engine. This is
possible with C<message>.

The methods always return undef.

=head2 uninstall, uninstall_internal, uninstall_incoming, uninstall_watcher

    $message->uninstall($event, $handler)
    $message->uninstall_incoming($event, $handler)
    $message->uninstall_internal($event, $handler)
    $message->uninstall_watcher($event, $handler)

If you do not specify a handler, then all handlers are going to be
uninstalled from the event. If you do specify one only this event will
be uninstalled.

The methods always return undef.

=head2 listen

    $message->listen()

Blocks the execution of the script from this point on and starts to
listen for events from the Engine.

=head2 message

    $message->message($name, $return_value, $id, ParamName => ParamValue, ...)

Sends a message (like chan.masquarade) to the Engine. Required
arguments are $name (name of the message) and $return_value (textual
return value of the message). If you want to specify parameters to be
returned with the message, but you do not want to give a specific $id
you can just set $id to undef or 0. Id is actually an obscure unique
message ID string. To see if your message is processed or not you
should call C<install_incoming>.

All passed arguments are escaped.

This method returns 1 on success and 0 on failure.

=head2 setlocal

    $message->setlocal($name, $value)

Requests a change of a local parameter. More information from
http://yate.null.ro/docs/extmodule.html.

=head2 params

    $message->params($params)

If no arguments are supplied, it returns the current parameters, else
it sets the parameters to the given ones and returns undef.

=head2 headers

    $message->headers($headers)

If no arguments are supplied it returns the current headers, else it
sets the headers to the given ones and returns undef.

=head2 param

    $message->param($key, $value)

If value is not specified returns the parameter matched by key or
undef if such parameter is not found. Else it sets the parameter to
the given value and returns undef.

=head2 header

    $message->header($key, $value)

If value is not specified returns the header matched by key or
undef if such header is not found. Else it sets the header
to the given value and returns undef.

=head2 print

    $message->print($message)

Sends a message directly to the engine. Use this method only if you
know what you are doing. If you send a wrong information the execution
of this script might not behave correctly or you can even block the
script. Do not use a trailing \n.

=head2 debug

    $message->debug($message)

Sends a debug message to the standart error stream. This message will
not be logged by the Engine or the Application itself. This is only
for debugging and should be used in development stage. Do not use a
trailing \n.

=head2 error

    $message->error($message)

Sends an error message to the standart error stream. Do not use a
trailing \n.

=head2 dump

    $message->dump()

Dumps all the information about the current Yate object.

=head2 escape

    $message->escape($string, $special_char)

Escapes the currrent string as stated in
http://yate.null.ro/docs/extmodule.html. Converts % into %% and every
character with ASCII code lower then 32 is converted to
%character("code + 32"). If you specify a special_char this character
will also be converted to the above code. This is only needed if you
pass arguments to the Engine. Special character is ":" in most of the
cases. Return value is escaped automatically, the same goes for the
parameters and headers you set. C<message> is also escaped.

=head2 output

    $message->output($message)

Sends a message to the Engine. This is the proper way of logging
messages for programs that connect to the socket interface as they may
not have the standard error redirected.

Anyway you will probably want to use your own logging like log to a
file instead of sending to the Engine.

=head2 retval, retvalue

    $message->retval($return_value)
    $message->retvalue($return_value)

Methods are depricated. See C<install> or SYNOPSYS for more
information.

=head1 INTERNAL METHODS

=head2 _install

    $message->_install($name, $type, $handler)

Because all install_ methods are similar this acts as an interface
to them. It puts the handler in an array for an event ($name) and
return 1 if this is the first entry or 0 else (error or another entry).

=head2 _uninstall

    $message->_uninstall($name, $type, $handler)

Because all uninstall_ methods are similar this acts as an interface
to them. It deletes a handler for an event from an array and return 1
if this is the last entry or 0 else (error or if there are more entries).

=head2 unescape

    $message->unescape($string)

Unescapes the current string. Every string passed to your handler will
be unescaped automatically.

=head2 output_error

    $message->output_error($message)

Sends a message to the standart error stream. This message is not
logged by anyone. Only for debugging propose.

=head2 parse_message

    $message->parse_message($line)

Parses a line received from the Engine and puts the headers and
parameters into the Yate object.

=head2 dispatch

    $message->dispatch()

Tries to call a handler for an event (uses information from the Yate
object).

=head2 return_message

    $message->return_message($processed, $return_value)

This method is called automatically by C<dispatch> if the handler
return true or message is not dispatched. Processed is either true or
false. return_value is the value you returned from your handler
subroutine. It sends a message to the Engine informing it if the event
is processed or not.

=head2 install_handlers

    $message->install_handlers()

Installs default handlers. Called automaticall on object creation.
Default handlers are C<handle_uninstall>, C<handle_install>,
C<handle_watch>, C<handle_unwatch>, C<handle_setlocal> and
C<handle_error_in>.

=head2 handle_install

    $message->handle_install()

Default handler for "install" messages received from Engine. This way
we can be sure that our handler for an event has either been added or
not.

=head2 handle_uninstall

    $message->handle_uninstall()

Default handler for "uninstall" messages received from Engine. This way
we can be sure that the removing of a handler has either succeeded or
not.

=head2 handle_watch

    $message->handle_watch()

Default handler for "watch" messages received from Engine. This way we
can be sure that our watcher for an event has either been added or not.

=head2 handle_uninstall

    $message->handle_uninstall()

Default handler for "uninstall" messages received from Engine. This way
we can be sure that the removing of a handler has either succeeded or
not.

=head2 handle_setlocal

    $message->handle_setlocal()

Default handler for "setlocal" messages received from Engine. This way
the module will let the program now, if the C<setlocal> call failed.

=head2 handle_error_in

    $message->handle_error_in()

Default handler for "Error in" messages received from Engine. Those are
messages that indicate fatal errors in a message we sent. Received
error is sent to the STDERR.

=head2 generate_id

    $message->generate_id()

Generates an unique random sequence of ten characters
(A..Z, a..z, 0-9).

=head1 EXAMPLES

See perl scripts in yate scripts/ directory.

=head1 TODO

- Connect (handle differenent channels).

=head1 SEE ALSO

Yate official website - http://yate.null.ro

Yate Application <=> Engine messages - http://yate.null.ro/docs/extmodule.html

=head1 BUGS

Please be so kind as to report any bugs at http://yate.null.ro/mantis/main_page.php.

=head1 COPYRIGHT AND LICENSE

Copyright (C) 2004-2006 Null Team

This library is free software; you can redistribute it and/or modify
it under the terms of the General Public License (GPL).  For
more information, see http://www.fsf.org/licenses/gpl.txt

=cut
