# --- Revised 3-Clause BSD License ---
# Copyright Semtech Corporation 2022. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
#     * Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright notice,
#       this list of conditions and the following disclaimer in the documentation
#       and/or other materials provided with the distribution.
#     * Neither the name of the Semtech corporation nor the names of its
#       contributors may be used to endorse or promote products derived from this
#       software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL SEMTECH CORPORATION. BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from typing import Optional,Union
import functools
import re
import struct
from base64 import b16decode as _b16decode



@functools.total_ordering
class Eui(object):
    """Class to represent EUIs. The normal representation is an string with 23 characters
    and a format of HH-...-HH. This matches the JSON and SQL representation.
    There are methods to convert to binary string or 64bit integer.
    """

    NUM_REGEX = re.compile("^(\-?\d+|0[xX][0-9A-Fa-f]+)$")
    """Standard decimal or hexadecimal number"""
    REGEX = re.compile("^([0-9a-fA-F]){2}(-([0-9a-fA-F]){2}){7}$")
    """Regular expression matching a well formed EUI"""
    REGEX2 = re.compile("^([0-9a-fA-F]){2}(:([0-9a-fA-F]){2}){7}$")
    """Regular expression matching a well formed EUI using colon instead of dashes"""


    @staticmethod
    def int2str (num:int) -> str:
        return "-".join(["{0:02X}".format((num>>(i*8)&0xFF)) for i in range(7,-1,-1)])

    @staticmethod
    def str2int (s:str) -> int:
        if Eui.NUM_REGEX.match(s):
            return int(s,0)
        if Eui.REGEX.match(s):
            b = _b16decode(s.replace("-","").encode('ascii'))
        elif Eui.REGEX2.match(s):
            b = _b16decode(s.replace(":","").encode('ascii'))
        else:
            raise ValueError("Illegal Eui: {}".format(s))
        return struct.unpack_from('>q',b)[0]

    @staticmethod
    def from_str(euistr:str) -> 'Eui':
        return Eui(euistr)

    @staticmethod
    def from_int(euinum:int) -> 'Eui':
        return Eui(euinum)

    @staticmethod
    def from_bytes(euibytes:bytes) -> 'Eui':
        return Eui(euibytes)

    def __bool__(self) -> bool:
        # Bool value is not dependent on EUI's int value
        return True

    def __index__(self) -> int:
        return struct.unpack_from('>q',self.as_bytes(),0)[0]

    def as_int(self) -> int:
        return self.__index__()

    def as_bytes(self) -> bytes:
        return _b16decode(self.euistr.replace("-","").encode('ascii'))

    def __str__(self):
        return self.euistr

    def __repr__(self) -> str:
        return "<{}: {}>".format(self.__class__.__name__, str(self))

    def __hash__(self):
        return hash(self.euistr)

    def __lt__(self, other):
        return self.euistr < other.euistr

    def __eq__(self, other):
        return True if isinstance(other,Eui) and self.euistr==other.euistr else False

    def __init__(self, euispec) -> None:
        if isinstance(euispec,Eui):
            self.euistr = euispec.euistr   # type: str
        elif isinstance(euispec,str):
            if Eui.NUM_REGEX.match(euispec):
                self.euistr = Eui.int2str(int(euispec,0))
            else:
                if Eui.REGEX.match(euispec):
                    self.euistr = euispec.upper()
                elif Eui.REGEX2.match(euispec):
                    self.euistr = euispec.replace(':','-').upper()
                else:
                    raise ValueError("Illegal EUI: {}".format(euispec))
        elif isinstance(euispec,bytes):
            if len(euispec)!=8:
                raise ValueError("Illegal EUI length: {}".format(len(euispec)))
            self.euistr = "-".join([ "{:02X}".format(euispec[i]) for i in range(8) ])
        elif isinstance(euispec,int):
            self.euistr = Eui.int2str(euispec)
        else:
            raise ValueError("Illegal EUI type: "+str(type(euispec)))





@functools.total_ordering
class Id6(object):
    """Class to represent 64bit unique ids. This is class very similar to Eui class but allows
    shorter text representations of the 64bit unique identifiers.
    The text encoding is analoguous to IPv6 text representation but covering 64 bits instead of 128 bits.
    """

    NUM_REGEX = re.compile("^(\-?\d+|0[xX][0-9A-Fa-f]+)$")
    """Standard decimal or hexadecimal number"""
    EUI_REGEX = re.compile("^([0-9a-fA-F]){2}(-([0-9a-fA-F]){2}){7}$")
    """Regular expression matching a well formed EUI"""
    EUI_REGEX2 = re.compile("^([0-9a-fA-F]){2}(:([0-9a-fA-F]){2}){7}$")
    """Regular expression matching a well formed EUI using colon instead of dashes"""
    ID6_REGEX = re.compile("^([0-9a-fA-F]){1,4}(:([0-9a-fA-F]){1,4}){3}$")
    """Id6 regular expression matching representation after :: expansion"""
    MAC_REGEX = re.compile("^([0-9a-fA-F]){2}(:([0-9a-fA-F]){2}){5}$")
    """Regular expression mathing a well-formed MAC addresss"""

    # Not used right now
    #ID6_REGEX_ALL = re.compile('(W:W:W:W|W::|W:W::|::W|::W:W|W::W)$'.replace('W','[0-9a-fA-F]{1,4}'))


    @staticmethod
    def int2str(id:int) -> str:
        if id==0:
            return '::0'

        if (id & 0xFFFFFFFF00000000) == 0:
            if (id & 0xFFFFFFFFFFFF0000) == 0:
                return "::%x" % (id & 0xFFFF)
            return "::%x:%x" % ((id>>16)&0xFFFF, id&0xFFFF)

        if (id & 0x00000000FFFFFFFF) == 0:
            if (id & 0x0000FFFFFFFFFFFF) == 0:
                return "%x::" % ((id>>48) & 0xFFFF)
            return "%x:%x::" % ((id>>48)&0xFFFF, (id>>32)&0xFFFF)

        if (id & 0x0000FFFFFFFF0000) == 0:
            return "%x::%x" % ((id>>48)&0xFFFF, id&0xFFFF)

        
        #if (id & 0x0000FFFF00000000) == 0:
        #    return "%x::%x:%x" % ((id>>48)&0xFFFF,
        #                          (id>>16)&0xFFFF,
        #                          id&0xFFFF)
        #
        #if (id & 0x00000000FFFF0000) == 0:
        #    return "%x:%x::%x" % ((id>>48)&0xFFFF,
        #                          (id>>32)&0xFFFF,
        #                          id&0xFFFF)

        return "%x:%x:%x:%x" % ((id>>48)&0xFFFF,
                                (id>>32)&0xFFFF,
                                (id>>16)&0xFFFF,
                                id&0xFFFF)


    @staticmethod
    def str2int(idstr:str) -> int:
        if Id6.NUM_REGEX.match(idstr):
            i = int(idstr,0)
        elif Id6.MAC_REGEX.match(idstr):
            i = int(idstr.replace(":",""), 16)
            i += 0x2<<56
        else:
            if idstr[0:2] == '::':
                if len(idstr)==2:
                    return 0
                try:
                    idstr.index(':',2)
                    s = '0:0:'
                except ValueError:
                    s = '0:0:0:'
                idstr = s+idstr[2:]

            elif idstr[-2:] == '::':
                if idstr.index(':') < len(idstr)-2:
                    s = ':0:0'
                else:
                    s = ':0:0:0'
                idstr = idstr[:-2]+s
            else:
                try:
                    p = idstr.index('::')
                    idstr = idstr[0:p]+':0:0:'+idstr[p+2:]
                except ValueError:
                    pass
            if not Id6.ID6_REGEX.match(idstr):
                if Id6.EUI_REGEX.match(idstr):
                    i = int(idstr.replace("-",""), 16)
                elif Id6.EUI_REGEX2.match(idstr):
                    i = int(idstr.replace(":",""), 16)
                else:
                    raise ValueError("Malformed Id6 representation: <"+idstr+">")
            else:
                a = idstr.split(':')
                i = ((int(a[0],16)<<48)|
                     (int(a[1],16)<<32)|
                     (int(a[2],16)<<16)|
                     (int(a[3],16)    ))
        if i >= 1<<63:
            i = (-1<<64) + i
        return i;

    @staticmethod
    def strx2int(idstr:str) -> int:
        """This function is the same as str2int execept that it translates MAC addresses
        into 64 bit by inserting 0xFFFE in the middle. This is the new scheme better suited
        for station2 deployments."""
        if Id6.MAC_REGEX.match(idstr):
            idstr = idstr.replace(":","")
            i = (int(idstr[0:6], 16) << 40) | int(idstr[6:12], 16) | 0xFFFE000000
            if i >= 1<<63:
                i = (-1<<64) + i
            return i;
        return Id6.str2int(idstr)

    def __bool__(self) -> bool:
        # Bool value is not dependent on internal int value
        return True

    def __index__(self) -> int:
        return self.id

    def mac2id(self,pos=0) -> int:
        return self.id | (pos<<(8*6))

    def as_mac_str(self) -> str:
        id = self.id
        if id & 0xFFFF000000 == 0xFFFE000000:
            id = (id & 0xFFFFFF) | ((id >> 16) & 0xFFFFFF000000)
        return ":".join(["{0:02X}".format((id>>(i*8)&0xFF)) for i in range(5,-1,-1)])

    def as_eui_str(self) -> str:
        return Eui.int2str(self.id)

    def as_int(self) -> int:
        return self.id

    def as_bytes(self) -> bytes:
        return struct.pack("<q", self.id)

    def __str__(self):
        if self.cat:
            return self.cat+'-'+self._idstr
        return self._idstr

    def __repr__(self) -> str:
        return "<{}: {}>".format(self.__class__.__name__, str(self))

    def __hash__(self):
        return hash((self.cat,self.id))

    def __lt__(self, other):
        if self.cat != other.cat:
            return self.cat < other.cat
        return self.id < other.id

    def __eq__(self, other):
        if not isinstance(other,Id6):
            return False
        return self.cat==other.cat and self.id==other.id

    def __init__(self, idspec:Union[int,str,'Id6',Eui], cat:str='') -> None:
        self.cat = cat
        if isinstance(idspec,Id6):
            self.id = idspec.id           # type: int
            self._idstr = idspec._idstr   # type: str
            self.cat = cat or idspec.cat  # type: str
        elif isinstance(idspec,str):
            try:
                pos = idspec.index('-')
                if pos > 0 and not Id6.EUI_REGEX.match(idspec):
                    self.cat = idspec[0:pos]
                    if cat and self.cat != cat:
                        raise ValueError('Ambiguous categories')
                    idspec = idspec[pos+1:]
            except:
                pass
            self.id = Id6.str2int(idspec)
            self._idstr = Id6.int2str(self.id)   # nomalized repr
        elif isinstance(idspec,int):
            if idspec >= 1<<63:
                idspec = (-1<<64) + idspec
            self.id = idspec
            self._idstr = Id6.int2str(self.id)
        elif isinstance(idspec,Eui):
            self.id = idspec.as_int()
            self._idstr = Id6.int2str(self.id)
        else:
            raise ValueError("Illegal Id6 type: "+str(type(idspec)))


