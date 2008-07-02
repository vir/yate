'''
setup.py
========

setup.py is part of the YAYPM library


Copyright 2006 Maciek Kaminski.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA


This is the distutils installation script for YAYPM.
'''
from distutils.core import setup
setup(
    name = 'yaypm',
    version = '0.2',
    description = 'Yet Another YATE Python Module',
    author = 'Maciek Kaminski',
    author_email = 'maciejka@tiger.com.pl',
    url = 'http://yate.null.ro/pmwiki/index.php?n=Main.YAYPM',
    packages = ['yaypm',
                'yaypm.utils',
                'yaypm.utils.resources',
                'yaypm.utils.tester',
                'yaypm.examples']
)
